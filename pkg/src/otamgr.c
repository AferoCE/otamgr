/* Copyright (c) 2017 Afero, Inc. All rights reserved. */

#include <stdint.h>
#include <stdlib.h>
#include <event2/event.h>
#include <event2/thread.h>
#include <unistd.h>
#include <errno.h>
#include "af_attr_client.h"
#include "af_log.h"

static struct event_base *sEventBase = NULL;
uint32_t g_debugLevel = 3;

#define OTA_PATH_PREFIX "/tmp"

static void on_get(uint32_t attributeId, uint16_t getId, void *context)
{
    if (attributeId == AF_ATTR_OTAMGR_OTA_UPD_PATH_PREFIX) {
        af_attr_send_get_response(AF_ATTR_STATUS_OK, getId, (uint8_t *)OTA_PATH_PREFIX, sizeof(OTA_PATH_PREFIX));
    } else {
        AFLOG_ERR("on_get_unknown_attr:attributeId=%d", attributeId);
    }
}

static void on_notify(uint32_t attributeId, uint8_t *value, int length, void *context)
{
    /* We have received the OTA notification. The value contains the path to the OTA image */
    AFLOG_INFO("ota_notification:path=%s", value);
}

static void on_open(int status, void *context)
{
    if (status != AF_ATTR_STATUS_OK) {
        AFLOG_ERR("open_failed:status=%d", status);
    }
}

static void on_close(int status, void *context)
{
    if (status != AF_ATTR_STATUS_OK) {
        AFLOG_ERR("unexpected_close:status=%d", status);
    }
}

int main(int argc, char *argv[])
{
    openlog("otamgr", LOG_PID, LOG_USER);

    sEventBase = event_base_new();
    if (sEventBase == NULL) {
        AFLOG_ERR("event_base_new_failed:errno=%d", errno);
        return AF_ATTR_STATUS_NO_SPACE;
    }

    af_attr_range_t r[] = {
        { AF_ATTR_HUBBY_OTA_UPGRADE_PATH, AF_ATTR_HUBBY_OTA_UPGRADE_PATH }
    };

    int status = af_attr_open(
        sEventBase,      /* event base */
        "IPC.OTAMGR",    /* owner name */
        sizeof(r) / sizeof(r[0]),  /* numRanges */
        r,               /* ranges */
        on_notify,       /* notifyCB */
        NULL,            /* ownerSetCB */
        on_get,          /* ownerGetCB */
        on_close,        /* closeCB */
        on_open,         /* openCB */
        NULL);           /* context */

    if (status != AF_ATTR_STATUS_OK) {
        AFLOG_ERR("af_attr_open:status=%d", status);
        event_base_free(sEventBase);
        return status;
    }

    event_base_dispatch(sEventBase);

    af_attr_close();

    event_base_free(sEventBase);

    closelog();
    return 0;
}
