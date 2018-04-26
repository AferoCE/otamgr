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

#ifdef BUILD_TARGET_DEBUG
uint32_t g_debugLevel = 3;
#else
uint32_t g_debugLevel = 1;
#endif

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

static void set_hubby_state(int state)
{
    /* This is where you would set the LED state */
    AFLOG_INFO("Setting hub state to %d", state);
}

static void on_hubby_state(uint8_t *value, int length)
{
    int hubbyState = *value;
    set_hubby_state(hubbyState);
}

#define REBOOT_REASON_FILE_PATH "/afero_nv/reboot_reason"

static void set_reboot_reason(char *reason)
{
    int fd = open(REBOOT_REASON_FILE_PATH, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IRGRP);
    if (fd < 0) {
        AFLOG_ERR("command_attribute_reboot_reason_open:errno=%d", errno);
    } else {
        int nw = write(fd, reason, strlen(reason));
        close(fd);
        if (nw < 0) {
            AFLOG_ERR("set_reboot_reason:errno=%d", errno);
        }
    }
}

#define REBOOT_REASON_FULL_OTA "full_ota"

static void on_ota_upgrade_path(uint8_t *value, int length)
{
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

    set_reboot_reason(REBOOT_REASON_FULL_OTA);

    /* upgrade; we should never return from this */
    af_util_system("/sbin/sysupgrade %s", imageSrcPath);
}

#define REBOOT_REASON_COMMAND "reboot_command"
#define REBOOT_REASON_FACTORY_RESET "factory_reset"

static void on_hubby_command(uint8_t *value, int length)
{
    if (length != 4) {
        AFLOG_WARNING("command_attribute_bad_size:length=%d",length);
    }
    if (length > 0) {
        uint8_t command = value[0];
        switch(command) {
            case 0x01 : /* reboot */
                sleep(5);
                set_reboot_reason(REBOOT_REASON_COMMAND);
                af_util_system("sync; /usr/bin/logpush ; reboot");
                break;
            case 0x02 : /* clear credentials */
            {
                sleep(4); /* allow four seconds before killing hubby to allow wifistad to erase the credentials */
                af_util_system("killall hubby");
                break;
            }
            case 0x03 : /* factory test mode */
                /* Add code here to put the device in factory test mode */
                /* This option is useful for reverse logistics (RMA)    */
                break;
        }
    }
}

static void on_notify(uint32_t attributeId, uint8_t *value, int length, void *context)
{
    switch (attributeId) {
        case AF_ATTR_HUBBY_STATE :
            on_hubby_state(value, length);
            break;
        case AF_ATTR_HUBBY_OTA_UPGRADE_PATH :
            on_ota_upgrade_path(value, length);
            break;
        case AF_ATTR_HUBBY_COMMAND :
            on_hubby_command(value, length);
            break;
        default :
            AFLOG_WARNING("notification_unknown:attributeId=%d", attributeId);
            break;
    }
}

static void on_get_hubby_state(uint8_t status, uint32_t attrId, uint8_t *value, int length, void *context)
{
    if (attrId == AF_ATTR_HUBBY_STATE) {
        if (length == 1) {
            set_hubby_state(*value);
        } else {
            AFLOG_WARNING("on_get_hubby_state_len:length=%d", length);
        }
    }
}

static void on_open(int status, void *context)
{
    if (status != AF_ATTR_STATUS_OK) {
        AFLOG_ERR("open_failed:status=%d", status);
        return;
    }
    status = af_attr_get(AF_ATTR_HUBBY_STATE, on_get_hubby_state, NULL);
    if (status != AF_ATTR_STATUS_OK) {
        AFLOG_WARNING("on_open_get_hubby_state:status=%d", status);
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
        return 1;
    }

    af_attr_range_t r[] = {
        { AF_ATTR_HUBBY_STATE, AF_ATTR_HUBBY_OTA_UPGRADE_PATH },
        { AF_ATTR_HUBBY_COMMAND, AF_ATTR_HUBBY_COMMAND }
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
        return 1;
    }

    event_base_dispatch(sEventBase);

    af_attr_close();

    event_base_free(sEventBase);

    closelog();
    return 0; /* control should never reach here */
}
