/* -*- mode: c; indent-tabs-mode: nil -*- */
/*
 * plugins/kdb/ldap/lockout.c
 *
 * Copyright (C) 2009 by the Massachusetts Institute of Technology.
 * All rights reserved.
 *
 * Export of this software from the United States of America may
 *   require a specific license from the United States Government.
 *   It is the responsibility of any person or organization contemplating
 *   export to obtain such a license before exporting.
 *
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of M.I.T. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  Furthermore if you modify this software you must label
 * your software as modified software and not distribute it in such a
 * fashion that it might be confused with the original M.I.T. software.
 * M.I.T. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 *
 *
 *
 */

#include <stdio.h>
#include <errno.h>

#include <k5-int.h>
#include <kadm5/admin.h>
#include <kdb.h>

#include "kdb_ldap.h"
#include "princ_xdr.h"
#include "ldap_principal.h"
#include "ldap_pwd_policy.h"
#include "ldap_tkt_policy.h"

static krb5_error_code
lookup_lockout_policy(krb5_context context,
                      krb5_db_entry *entry,
                      krb5_kvno *pw_max_fail,
                      krb5_deltat *pw_failcnt_interval,
                      krb5_deltat *pw_lockout_duration)
{
    krb5_tl_data tl_data;
    krb5_error_code code;
    osa_princ_ent_rec adb;
    XDR xdrs;

    *pw_max_fail = 0;
    *pw_failcnt_interval = 0;
    *pw_lockout_duration = 0;

    tl_data.tl_data_type = KRB5_TL_KADM_DATA;

    code = krb5_dbe_lookup_tl_data(context, entry, &tl_data);
    if (code != 0 || tl_data.tl_data_length == 0)
        return code;

    memset(&adb, 0, sizeof(adb));

    code = krb5_lookup_tl_kadm_data(&tl_data, &adb);
    if (code != 0)
        return code;

    if (adb.policy != NULL) {
        osa_policy_ent_t policy = NULL;
        int count = 0;

        code = krb5_ldap_get_password_policy(context, adb.policy,
                                             &policy, &count);
        if (code == 0 && count == 1) {
            *pw_max_fail = policy->pw_max_fail;
            *pw_failcnt_interval = policy->pw_failcnt_interval;
            *pw_lockout_duration = policy->pw_lockout_duration;
        }
        krb5_ldap_free_password_policy(context, policy);
    }

    xdrmem_create(&xdrs, NULL, 0, XDR_FREE);
    ldap_xdr_osa_princ_ent_rec(&xdrs, &adb);
    xdr_destroy(&xdrs);

    return 0;
}

/* draft-behera-ldap-password-policy-10.txt 7.1 */
static krb5_boolean
locked_check_p(krb5_context context,
               krb5_timestamp stamp,
               krb5_timestamp locked_time,
               krb5_timestamp lockout_duration)
{
    if (locked_time == 0)
        return FALSE;

    if (lockout_duration == 0)
        return TRUE; /* account permanently locked */

    return (stamp < locked_time + lockout_duration);
}

krb5_error_code
krb5_ldap_lockout_check_policy(krb5_context context,
                               krb5_db_entry *entry,
                               krb5_timestamp stamp)
{
    krb5_error_code code;
    krb5_timestamp locked_time = 0;
    krb5_kvno max_fail = 0;
    krb5_deltat failcnt_interval = 0;
    krb5_deltat lockout_duration = 0;

    code = krb5_dbe_lookup_locked_time(context, entry, &locked_time);
    if (code != 0 || locked_time == 0)
        return code;

    code = lookup_lockout_policy(context, entry, &max_fail,
                                 &failcnt_interval,
                                 &lockout_duration);
    if (code != 0)
        return code;

    if (locked_check_p(context, stamp, locked_time, lockout_duration))
        return KRB5KDC_ERR_CLIENT_REVOKED;

    return 0;
}

krb5_error_code
krb5_ldap_lockout_audit(krb5_context context,
                        krb5_db_entry *entry,
                        krb5_timestamp stamp,
                        krb5_error_code status)
{
    krb5_error_code code;
    krb5_timestamp locked_time = 0;
    krb5_kvno max_fail = 0;
    krb5_deltat failcnt_interval = 0;
    krb5_deltat lockout_duration = 0;
    int nentries = 1;

    switch (status) {
    case 0:
    case KRB5KDC_ERR_PREAUTH_FAILED:
    case KRB5KRB_AP_ERR_BAD_INTEGRITY:
        break;
    default:
        return 0;
    }

    krb5_dbe_lookup_locked_time(context, entry, &locked_time);

    code = lookup_lockout_policy(context, entry, &max_fail,
                                 &failcnt_interval,
                                 &lockout_duration);
    if (code != 0)
        return code;

    assert(!locked_check_p(context, stamp, locked_time, lockout_duration));

    entry->mask = 0;

    if (status == 0 && (entry->attributes & KRB5_KDB_REQUIRES_PRE_AUTH)) {
        /*
         * Only mark the authentication as successful if the entry
         * required preauthentication, otherwise we have no idea.
         */
        entry->fail_auth_count = 0;
        if (locked_time != 0) {
            locked_time = 0;
            entry->mask |= KADM5_LOCKED_TIME;
        }
        entry->last_success = stamp;
        entry->mask |= KADM5_LAST_SUCCESS | KADM5_FAIL_AUTH_COUNT;
    } else if (status == KRB5KDC_ERR_PREAUTH_FAILED ||
               status == KRB5KRB_AP_ERR_BAD_INTEGRITY) {
        if (failcnt_interval != 0 &&
            stamp > entry->last_failed + failcnt_interval) {
            /* Automatically unlock account after failcnt_interval */
            entry->fail_auth_count = 0;
            locked_time = 0;
            entry->mask |= KADM5_LOCKED_TIME;
        }

        entry->last_failed = stamp;
        entry->mask |= KADM5_LAST_FAILED | KADM5_FAIL_AUTH_COUNT_INCREMENT;

        if (max_fail != 0 &&
            entry->fail_auth_count >= max_fail) {
            locked_time = stamp;
            entry->mask |= KADM5_LOCKED_TIME;
        }
    }

    if (entry->mask & KADM5_LOCKED_TIME) {
        code = krb5_dbe_update_locked_time(context, entry, locked_time);
        if (code != 0)
            return code;
    }

    if (entry->mask) {
        code = krb5_ldap_put_principal(context, entry,
                                       &nentries, NULL);
        if (code != 0)
            return code;
    }

    return 0;
}