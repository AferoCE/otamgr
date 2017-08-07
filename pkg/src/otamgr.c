/* Copyright (c) 2017 Afero, Inc. All rights reserved. */

#include <stdint.h>
#include <stdlib.h>
#include <event2/event.h>
#include <event2/thread.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <fcntl.h>
#include "af_attr_client.h"
#include "af_log.h"
#include "af_util.h"

static struct event_base *sEventBase = NULL;
uint32_t g_debugLevel = 3;

#define OTA_PATH_PREFIX "/tmp"
#define MAX_PATH_LEN 256
#define KEEP_FILE_PATH "/lib/upgrade/keep.d/afero_ota"

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
    if (attributeId != AF_ATTR_HUBBY_OTA_UPGRADE_PATH) {
        AFLOG_WARNING("notification_unknown:attributeId=%d", attributeId);
        return;
    }

    /* We have received the OTA notification. The value contains the path to the OTA image */
    AFLOG_INFO("ota_notification:path=%s", value);

    char imageSrcPath[MAX_PATH_LEN];
    char headerSrcPath[MAX_PATH_LEN];
    char headerDstPath[MAX_PATH_LEN];

    int len;
    len = snprintf(imageSrcPath, MAX_PATH_LEN, "%s.img", value);
    if (len >= MAX_PATH_LEN) {
        AFLOG_ERR("ota_img_path_len:path=\"%s\":len=%d", value, strlen((char *)value));
        return;
    }

    len = snprintf(headerSrcPath, MAX_PATH_LEN, "%s.hdr", value);
    if (len >= MAX_PATH_LEN) {
        AFLOG_ERR("ota_hdr_path_len:path=\"%s\":len=%d", value, strlen((char *)value));
        return;
    }

    /* figure out the base name */
    char *baseName = NULL;

    len--;
    while (len > 0) {
        if (headerSrcPath[len] == '/') {
            baseName = &headerSrcPath[len+1];
            break;
        }
        len--;
    }
    if (baseName == NULL) {
        baseName = headerSrcPath;
    }

    /* figure out the destination path for header */
    len = snprintf(headerDstPath, MAX_PATH_LEN, "/etc/%s", baseName);

    AFLOG_DEBUG1("otamgr_image_src:path=%s", imageSrcPath);
    AFLOG_DEBUG1("otamgr_header_src:path=%s", headerSrcPath);
    AFLOG_DEBUG1("otamgr_header_dst:path=%s", headerDstPath);

    /* On OpenWRT, we have a mechanism for saving files across upgrades */
    /* We store a file in /lib/upgrade/keep.d/afero_ota containing the  */
    /* header destination path. OpenWRT will automatically restore this */
    /* file when the upgrade is completed.                              */
    unlink(KEEP_FILE_PATH);
    int fd = open(KEEP_FILE_PATH, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        AFLOG_ERR("otamgr_keep_file_create:keep_path=" KEEP_FILE_PATH ":can't create keep file--aborting");
        return;
    } else {
        write(fd, headerDstPath, strlen((char *)headerDstPath));
        close(fd);
    }

    /* now copy the header file onto the root file system */
    /* TODO consider making this more secure by checking path arguments for redirects, etc. */
    int ret = af_util_system("/bin/cp %s %s", headerSrcPath, headerDstPath);
    if (ret != 0) {
        AFLOG_WARNING("otamgr_cp:ret=%d:header file copy to root failed", ret);
    }

    /* push the logs */
    ret = af_util_system("/usr/bin/logpush < /dev/null");
    if (ret != 0) {
        AFLOG_WARNING("otamgr_logpush:ret=%d:logpush failed", ret);
    }

    /* upgrade; we should never return from this */
    af_util_system("/sbin/sysupgrade %s", imageSrcPath);
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
