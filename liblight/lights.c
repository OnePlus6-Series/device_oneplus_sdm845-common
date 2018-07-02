/*
 * Copyright (C) 2014, 2017-2018 The  Linux Foundation. All rights reserved.
 * Not a contribution
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "qdlights"

#include <log/log.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>

#include <hardware/lights.h>

/******************************************************************************/

static pthread_once_t g_init = PTHREAD_ONCE_INIT;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static struct light_state_t g_attention;
static struct light_state_t g_notification;
static struct light_state_t g_battery;

#define LCD_BRIGHTNESS_FILE "/sys/class/backlight/panel0-backlight/brightness"
#define LCD_MAX_BRIGHTNESS_FILE "/sys/class/backlight/panel0-backlight/max_brightness"

#define RED_LED_FILE "/sys/class/leds/red/brightness"
#define RED_DUTY_PCTS_FILE "/sys/class/leds/red/duty_pcts"
#define RED_START_IDX_FILE "/sys/class/leds/red/start_idx"
#define RED_PAUSE_LO_FILE "/sys/class/leds/red/pause_lo"
#define RED_PAUSE_HI_FILE "/sys/class/leds/red/pause_hi"
#define RED_RAMP_STEP_MS_FILE "/sys/class/leds/red/ramp_step_ms"
#define RED_BLINK_FILE "/sys/class/leds/red/blink"

#define GREEN_LED_FILE "/sys/class/leds/green/brightness"
#define GREEN_DUTY_PCTS_FILE "/sys/class/leds/green/duty_pcts"
#define GREEN_START_IDX_FILE "/sys/class/leds/green/start_idx"
#define GREEN_PAUSE_LO_FILE "/sys/class/leds/green/pause_lo"
#define GREEN_PAUSE_HI_FILE "/sys/class/leds/green/pause_hi"
#define GREEN_RAMP_STEP_MS_FILE "/sys/class/leds/green/ramp_step_ms"
#define GREEN_BLINK_FILE "/sys/class/leds/green/blink"

#define BLUE_LED_FILE "/sys/class/leds/blue/brightness"
#define BLUE_DUTY_PCTS_FILE "/sys/class/leds/blue/duty_pcts"
#define BLUE_START_IDX_FILE "/sys/class/leds/blue/start_idx"
#define BLUE_PAUSE_LO_FILE "/sys/class/leds/blue/pause_lo"
#define BLUE_PAUSE_HI_FILE "/sys/class/leds/blue/pause_hi"
#define BLUE_RAMP_STEP_MS_FILE "/sys/class/leds/blue/ramp_step_ms"
#define BLUE_BLINK_FILE "/sys/class/leds/blue/blink"

#define RAMP_SIZE 8
static int BRIGHTNESS_RAMP[RAMP_SIZE] = { 0, 12, 25, 37, 50, 72, 85, 100 };
#define RAMP_STEP_DURATION 50

#define DEFAULT_MAX_BRIGHTNESS 255
int max_brightness;

/**
 * device methods
 */

void init_globals(void)
{
    // init the mutex
    pthread_mutex_init(&g_lock, NULL);
}

static int read_int(char const *path)
{
    int fd, len;
    int num_bytes = 10;
    char buf[11];
    int retval;

    if ((fd = open(path, O_RDONLY)) < 0) {
        ALOGE("%s: failed to open %s\n", __func__, path);
        goto fail;
    }

    if ((len = read(fd, buf, num_bytes - 1)) < 0) {
        ALOGE("%s: failed to read from %s\n", __func__, path);
        goto fail;
    }

    buf[len] = '\0';
    close(fd);

    // no endptr, decimal base
    retval = strtol(buf, NULL, 10);
    return retval == 0 ? -1 : retval;

fail:
    if (fd >= 0) {
        close(fd);
    }
    return -1;
}

static int write_int(char *path, int value)
{
    int fd;
    static int already_warned = 0;

    if ((fd = open(path, O_RDWR)) >= 0) {
        char buffer[20];
        int bytes = snprintf(buffer, sizeof(buffer), "%d\n", value);
        ssize_t amt = write(fd, buffer, (size_t)bytes);
        close(fd);
        return amt == -1 ? -errno : 0;
    } else {
        if (already_warned == 0) {
            ALOGE("write_int failed to open %s, errno = %d\n", path, errno);
            already_warned = 1;
        }
        return -errno;
    }
}

static int write_str(char const *path, char *value)
{
    int fd;
    static int already_warned = 0;

    if ((fd = open(path, O_RDWR)) >= 0) {
        char buffer[1024];
        int bytes = snprintf(buffer, sizeof(buffer), "%s\n", value);
        ssize_t amt = write(fd, buffer, (size_t) bytes);
        close(fd);
        return amt == -1 ? -errno : 0;
    } else {
        if (already_warned == 0) {
            ALOGE("write_str failed to open %s\n", path);
            already_warned = 1;
        }
        return -errno;
    }
}

static char *get_scaled_duty_pcts(int brightness)
{
    char *buf = malloc(5 * RAMP_SIZE * sizeof(char));
    char *pad = "";
    int i = 0;

    memset(buf, 0, 5 * RAMP_SIZE * sizeof(char));

    for (i = 0; i < RAMP_SIZE; i++) {
        char temp[5] = "";
        snprintf(temp, sizeof(temp), "%s%d", pad, (BRIGHTNESS_RAMP[i] * brightness / 255));
        strcat(buf, temp);
        pad = ",";
    }
    ALOGV("%s: brightness=%d duty=%s", __func__, brightness, buf);
    return buf;
}

static int
is_lit(struct light_state_t const *state)
{
    return state->color & 0x00ffffff;
}

static int
rgb_to_brightness(struct light_state_t const *state)
{
    int color = state->color & 0x00ffffff;
    return ((77 * ((color>>16) & 0x00ff))
            + (150 * ((color>>8) & 0x00ff)) + (29 * (color & 0x00ff))) >> 8;
}

static int
set_light_backlight(struct light_device_t *dev,
        struct light_state_t const *state)
{
    int err = 0;
    int brightness = rgb_to_brightness(state);

    if (!dev) {
        return -1;
    }

    pthread_mutex_lock(&g_lock);

    if (!err) {
        // If max panel brightness is not the default (255),
        // apply linear scaling across the accepted range.
        if (max_brightness != DEFAULT_MAX_BRIGHTNESS) {
            int old_brightness = brightness;
            brightness = brightness * max_brightness / DEFAULT_MAX_BRIGHTNESS;
            ALOGV("%s: scaling brightness %d => %d\n", __func__, old_brightness, brightness);
        }

        if (!access(LCD_BRIGHTNESS_FILE, F_OK)) {
            err = write_int(LCD_BRIGHTNESS_FILE, brightness);
        }
    }

    pthread_mutex_unlock(&g_lock);
    return err;
}

static int
set_speaker_light_locked(struct light_device_t *dev,
        struct light_state_t const *state)
{
    int red, green, blue, blink;
    int onMS, offMS, stepDuration, pauseHi;
    unsigned int colorRGB;
    char *duty;

    if (!dev) {
        return -1;
    }

    switch (state->flashMode) {
        case LIGHT_FLASH_TIMED:
            onMS = state->flashOnMS;
            offMS = state->flashOffMS;
            break;
        case LIGHT_FLASH_NONE:
        default:
            onMS = 0;
            offMS = 0;
            break;
    }

    colorRGB = state->color;
    red = (colorRGB >> 16) & 0xff;
    green = (colorRGB >> 8) & 0xff;
    blue = colorRGB & 0xff;
    blink = onMS > 0 && offMS > 0;

    // Disable all blinking to start
    write_int(RED_BLINK_FILE, 0);
    write_int(GREEN_BLINK_FILE, 0);
    write_int(BLUE_BLINK_FILE, 0);

    if (blink) {
        stepDuration = RAMP_STEP_DURATION;
        pauseHi = onMS - (stepDuration * RAMP_SIZE * 2);
        if (stepDuration * RAMP_SIZE * 2 > onMS) {
            stepDuration = onMS / (RAMP_SIZE * 2);
            pauseHi = 0;
        }

        // Red
        write_int(RED_START_IDX_FILE, 0);
        duty = get_scaled_duty_pcts(red);
        write_str(RED_DUTY_PCTS_FILE, duty);
        write_int(RED_PAUSE_LO_FILE, offMS);
        // The led driver is configured to ramp up then ramp
        // down the lut. This effectively doubles the ramp duration.
        write_int(RED_PAUSE_HI_FILE, pauseHi);
        write_int(RED_RAMP_STEP_MS_FILE, stepDuration);
        free(duty);

        // Green
        write_int(GREEN_START_IDX_FILE, RAMP_SIZE);
        duty = get_scaled_duty_pcts(green);
        write_str(GREEN_DUTY_PCTS_FILE, duty);
        write_int(GREEN_PAUSE_LO_FILE, offMS);
        // The led driver is configured to ramp up then ramp
        // down the lut. This effectively doubles the ramp duration.
        write_int(GREEN_PAUSE_HI_FILE, pauseHi);
        write_int(GREEN_RAMP_STEP_MS_FILE, stepDuration);
        free(duty);

        // Blue
        write_int(BLUE_START_IDX_FILE, RAMP_SIZE * 2);
        duty = get_scaled_duty_pcts(blue);
        write_str(BLUE_DUTY_PCTS_FILE, duty);
        write_int(BLUE_PAUSE_LO_FILE, offMS);
        // The led driver is configured to ramp up then ramp
        // down the lut. This effectively doubles the ramp duration.
        write_int(BLUE_PAUSE_HI_FILE, pauseHi);
        write_int(BLUE_RAMP_STEP_MS_FILE, stepDuration);
        free(duty);

        // Start the party
        write_int(RED_BLINK_FILE, red);
        write_int(GREEN_BLINK_FILE, green);
        write_int(BLUE_BLINK_FILE, blue);
    } else {
        write_int(RED_LED_FILE, red);
        write_int(GREEN_LED_FILE, green);
        write_int(BLUE_LED_FILE, blue);
    }

    ALOGD("set_speaker_light_locked mode=%d, colorRGB=%08X, onMS=%d, offMS=%d\n",
            state->flashMode, colorRGB, onMS, offMS);

    return 0;
}

static void
handle_speaker_light_locked(struct light_device_t *dev)
{
    if (is_lit(&g_attention)) {
        set_speaker_light_locked(dev, &g_attention);
    } else if (is_lit(&g_notification)) {
        set_speaker_light_locked(dev, &g_notification);
    } else {
        set_speaker_light_locked(dev, &g_battery);
    }
}

static int
set_light_battery(struct light_device_t *dev,
        struct light_state_t const *state)
{
    pthread_mutex_lock(&g_lock);
    g_battery = *state;
    handle_speaker_light_locked(dev);
    pthread_mutex_unlock(&g_lock);
    return 0;
}

static int
set_light_notifications(struct light_device_t *dev,
        struct light_state_t const *state)
{
    pthread_mutex_lock(&g_lock);
    g_notification = *state;
    handle_speaker_light_locked(dev);
    pthread_mutex_unlock(&g_lock);
    return 0;
}

static int
set_light_attention(struct light_device_t *dev,
        struct light_state_t const *state)
{
    pthread_mutex_lock(&g_lock);
    g_attention = *state;
    handle_speaker_light_locked(dev);
    pthread_mutex_unlock(&g_lock);
    return 0;
}

/** Close the lights device */
static int
close_lights(struct light_device_t *dev)
{
    if (dev) {
        free(dev);
    }
    return 0;
}


/******************************************************************************/

/**
 * module methods
 */

/** Open a new instance of a lights device using name */
static int open_lights(const struct hw_module_t *module, char const *name,
        struct hw_device_t **device)
{
    int (*set_light)(struct light_device_t *dev,
            struct light_state_t const *state);

    if (0 == strcmp(LIGHT_ID_BACKLIGHT, name)) {
        set_light = set_light_backlight;
    } else if (0 == strcmp(LIGHT_ID_BATTERY, name)) {
        set_light = set_light_battery;
    } else if (0 == strcmp(LIGHT_ID_NOTIFICATIONS, name)) {
        set_light = set_light_notifications;
    } else if (0 == strcmp(LIGHT_ID_ATTENTION, name)) {
        set_light = set_light_attention;
    } else {
        return -EINVAL;
    }

    if ((max_brightness = read_int(LCD_MAX_BRIGHTNESS_FILE)) < 0) {
        ALOGE("%s: failed to read max panel brightness, fallback to 255!\n", __func__);
        max_brightness = DEFAULT_MAX_BRIGHTNESS;
    }

    pthread_once(&g_init, init_globals);

    struct light_device_t *dev = malloc(sizeof(struct light_device_t));

    if (!dev) {
        return -ENOMEM;
    }

    memset(dev, 0, sizeof(*dev));

    dev->common.tag = HARDWARE_DEVICE_TAG;
    dev->common.version = LIGHTS_DEVICE_API_VERSION_2_0;
    dev->common.module = (struct hw_module_t *) module;
    dev->common.close = (int (*)(struct hw_device_t *)) close_lights;
    dev->set_light = set_light;

    *device = (struct hw_device_t *) dev;
    return 0;
}

static struct hw_module_methods_t lights_module_methods = {
    .open =  open_lights,
};

/*
 * The lights Module
 */
struct hw_module_t HAL_MODULE_INFO_SYM = {
    .tag = HARDWARE_MODULE_TAG,
    .version_major = 1,
    .version_minor = 0,
    .id = LIGHTS_HARDWARE_MODULE_ID,
    .name = "lights Module",
    .author = "Google, Inc.",
    .methods = &lights_module_methods,
};
