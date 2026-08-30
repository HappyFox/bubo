/**
 * @file bsp_camera.c
 * @brief Teaching source for 5inch_P4_IDF_13_Camera_Real_Time.
 *
 * This file is part of the CrowPanel Advanced 5-inch ESP32-P4 course.
 * The comments explain module responsibilities and observable behavior
 * without changing the original program logic.
 */

/*————————————————————————————————————————Header file declaration————————————————————————————————————————*/
#include "bsp_camera.h"
/*——————————————————————————————————————Header file declaration end——————————————————————————————————————*/

/*——————————————————————————————————————————Variable declaration—————————————————————————————————————————*/
static i2c_master_bus_handle_t sccb_bus_handle = NULL;
static camera_video_t camera_video;
uint8_t *cam_buffer[2];
size_t cam_buffer_size[2];
static camera_frame_analysis_cb_t analysis_frame_cb;
static uint8_t analysis_frame_counter;
int camera_video_id = 0;
/*————————————————————————————————————————Variable declaration end———————————————————————————————————————*/

/*—————————————————————————————————————————Functional function———————————————————————————————————————————*/

esp_err_t camera_video_init()
{
    esp_err_t err = ESP_OK;
    i2c_master_bus_config_t sccb_conf = {
        .i2c_port = SCCB_MASTER_PORT,
        .sda_io_num = SCCB_GPIO_SDA,
        .scl_io_num = SCCB_GPIO_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .flags.enable_internal_pullup = true,
    };
    CAMERA_INFO("Initializing SCCB Bus.......");
    err = i2c_new_master_bus(&sccb_conf, &sccb_bus_handle);
    esp_video_init_csi_config_t csi_config = {
        .sccb_config = {
            .init_sccb = true,
            .i2c_config = {
                .port = SCCB_MASTER_PORT,
                .scl_pin = SCCB_GPIO_SCL,
                .sda_pin = SCCB_GPIO_SDA,
            },
            .freq = 100000,
        },
        .reset_pin = -1,
        .pwdn_pin = -1,
    };
    csi_config.sccb_config.init_sccb = false;
    csi_config.sccb_config.i2c_handle = sccb_bus_handle;
    esp_video_init_config_t cam_config_ptr = {
        .csi = &csi_config,
    };
    err = esp_video_init(&cam_config_ptr);
    if (err != ESP_OK)
        return err;
    return err;
}

/**
 * @brief Perform the video open operation.
 *
 * Called by the application when this module operation is required.
 * @return Result produced by the operation; see the function implementation for success and error values.
 */
int video_open()
{
    struct v4l2_format camera_format;
    struct v4l2_capability capability;
#if CONFIG_ENABLE_CAM_SENSOR_PIC_VFLIP || CONFIG_ENABLE_CAM_SENSOR_PIC_HFLIP
    struct v4l2_ext_controls controls;
    struct v4l2_ext_control control[1];
#endif
    int fd = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDONLY | O_NONBLOCK, 0);
    if (fd < 0)
    {
        CAMERA_ERROR("Open video failed");
        return -1;
    }
    if (ioctl(fd, VIDIOC_QUERYCAP, &capability))
    {
        CAMERA_ERROR("failed to get capability");
        goto exit_0;
    }
    CAMERA_INFO("version: %d.%d.%d", (uint16_t)(capability.version >> 16), (uint8_t)(capability.version >> 8), (uint8_t)capability.version);
    CAMERA_INFO("driver:  %s", capability.driver);
    CAMERA_INFO("card:    %s", capability.card);
    CAMERA_INFO("bus:     %s", capability.bus_info);
    memset(&camera_format, 0, sizeof(struct v4l2_format));
    camera_format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_G_FMT, &camera_format) != 0)
    {
        CAMERA_ERROR("failed to get format");
        goto exit_0;
    }
    uint32_t fmt = camera_format.fmt.pix.pixelformat;
    uint32_t bpl = camera_format.fmt.pix.bytesperline;
    CAMERA_INFO("width=%" PRIu32 " height=%" PRIu32 " format=%" PRIu32 "(0x%" PRIx32 ") bytesperline=%" PRIu32, camera_format.fmt.pix.width, camera_format.fmt.pix.height, fmt, fmt, bpl);
    CAMERA_INFO("checking if format RGB565: %d (V4L2_PIX_FMT_RGB565=0x%" PRIx32 ")", fmt != V4L2_PIX_FMT_RGB565, V4L2_PIX_FMT_RGB565);
    camera_video.camera_buf_hes = camera_format.fmt.pix.width;
    camera_video.camera_buf_ves = camera_format.fmt.pix.height;
    camera_video.camera_bytesperline = camera_format.fmt.pix.bytesperline;
    if (camera_format.fmt.pix.pixelformat != V4L2_PIX_FMT_RGB565)
    {
        CAMERA_INFO("format is NOT RGB565, setting RGB565");
        struct v4l2_format format = {
            .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .fmt.pix.width = camera_format.fmt.pix.width,
            .fmt.pix.height = camera_format.fmt.pix.height,
            .fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565,
        };
        CAMERA_INFO("before S_FMT: width=%" PRIu32 " height=%" PRIu32 " format=%" PRIu32, format.fmt.pix.width, format.fmt.pix.height, format.fmt.pix.pixelformat);
        if (ioctl(fd, VIDIOC_S_FMT, &format) != 0)
        {
            CAMERA_ERROR("failed to set format");
            goto exit_0;
        }
        CAMERA_INFO("VIDIOC_S_FMT succeeded");
        camera_format = format;
        if (ioctl(fd, VIDIOC_G_FMT, &camera_format) != 0)
        {
            CAMERA_ERROR("failed to get format after set");
            goto exit_0;
        }
        CAMERA_INFO("after S_FMT: width=%" PRIu32 " height=%" PRIu32 " format=%" PRIu32 " bytesperline=%d", camera_format.fmt.pix.width, camera_format.fmt.pix.height, camera_format.fmt.pix.pixelformat, camera_format.fmt.pix.bytesperline);
        CAMERA_INFO("format after S_FMT is RGB565: %d", camera_format.fmt.pix.pixelformat == V4L2_PIX_FMT_RGB565);
        if (camera_format.fmt.pix.pixelformat != V4L2_PIX_FMT_RGB565)
        {
            CAMERA_ERROR("format negotiation failed: requested RGB565 but got format=%" PRIu32, camera_format.fmt.pix.pixelformat);
            goto exit_0;
        }
    }
    camera_video.camera_bytesperline = camera_format.fmt.pix.bytesperline;
    CAMERA_INFO("final bytesperline=%d", camera_video.camera_bytesperline);
    CAMERA_INFO("app_video_open successful");
#if CONFIG_ENABLE_CAM_SENSOR_PIC_VFLIP
    controls.ctrl_class = V4L2_CTRL_CLASS_USER;
    controls.count = 1;
    controls.controls = control;
    control[0].id = V4L2_CID_VFLIP;
    control[0].value = 1;
    if (ioctl(fd, VIDIOC_S_EXT_CTRLS, &controls) != 0)
    {
        CAMERA_ERROR("failed to mirror the frame horizontally and skip this step");
    }
#endif
#if CONFIG_ENABLE_CAM_SENSOR_PIC_HFLIP
    controls.ctrl_class = V4L2_CTRL_CLASS_USER;
    controls.count = 1;
    controls.controls = control;
    control[0].id = V4L2_CID_HFLIP;
    control[0].value = 1;
    if (ioctl(fd, VIDIOC_S_EXT_CTRLS, &controls) != 0)
    {
        CAMERA_ERROR("failed to mirror the frame horizontally and skip this step");
    }
#endif
    CAMERA_INFO("fd = %d", fd);
    return fd;
exit_0:
    close(fd);
    return -1;
}

/**
 * @brief Perform the camera video set bufs operation.
 *
 * Called by the application when this module operation is required.
 * @param video_fd Input or output value used by this operation.
 * @param fb_num Input or output value used by this operation.
 * @param fb Input or output value used by this operation.
 * @return Result produced by the operation; see the function implementation for success and error values.
 */
esp_err_t camera_video_set_bufs(int video_fd, uint32_t fb_num, const void **fb)
{
    struct v4l2_requestbuffers req;
    if (fb_num > MAX_BUFFER_COUNT)
    {
        CAMERA_ERROR("buffer num is too large");
        return ESP_FAIL;
    }
    else if (fb_num < 2)
    {
        CAMERA_ERROR("At least two buffers are required");
        return ESP_FAIL;
    }
    memset(&req, 0, sizeof(req));
    req.count = fb_num;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    camera_video.camera_mem_mode = req.memory = fb ? V4L2_MEMORY_USERPTR : V4L2_MEMORY_MMAP;
    if (ioctl(video_fd, VIDIOC_REQBUFS, &req) != 0)
    {
        CAMERA_ERROR("req bufs failed");
        goto errout_req_bufs;
    }
    for (int i = 0; i < fb_num; i++)
    {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = req.memory;
        buf.index = i;
        if (ioctl(video_fd, VIDIOC_QUERYBUF, &buf) != 0)
        {
            CAMERA_ERROR("query buf failed");
            goto errout_req_bufs;
        }
        if (req.memory == V4L2_MEMORY_MMAP)
        {
            camera_video.camera_buffer[i] = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, video_fd, buf.m.offset);
            if (camera_video.camera_buffer[i] == NULL)
            {
                CAMERA_ERROR("mmap failed");
                goto errout_req_bufs;
            }
        }
        else
        {
            if (!fb[i])
            {
                CAMERA_ERROR("frame buffer is NULL");
                goto errout_req_bufs;
            }
            buf.m.userptr = (unsigned long)fb[i];
            camera_video.camera_buffer[i] = (uint8_t *)fb[i];
        }
        camera_video.camera_buf_size = buf.length;
        if (ioctl(video_fd, VIDIOC_QBUF, &buf) != 0)
        {
            CAMERA_ERROR("queue frame buffer failed");
            goto errout_req_bufs;
        }
    }
    return ESP_OK;
errout_req_bufs:
    close(video_fd);
    return ESP_FAIL;
}

/**
 * @brief Perform the camera video get bufs operation.
 *
 * Called by the application when this module operation is required.
 * @param fb_num Input or output value used by this operation.
 * @param fb Input or output value used by this operation.
 * @return Result produced by the operation; see the function implementation for success and error values.
 */
esp_err_t camera_video_get_bufs(int fb_num, void **fb)
{
    if (fb_num > MAX_BUFFER_COUNT)
    {
        CAMERA_ERROR("buffer num is too large");
        return ESP_FAIL;
    }
    else if (fb_num < 2)
    {
        CAMERA_ERROR("At least two buffers are required");
        return ESP_FAIL;
    }
    for (int i = 0; i < fb_num; i++)
    {
        if (camera_video.camera_buffer[i] != NULL)
        {
            fb[i] = camera_video.camera_buffer[i];
        }
        else
        {
            CAMERA_ERROR("frame buffer is NULL");
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

/**
 * @brief Perform the app video get buf size operation.
 *
 * Called by the application when this module operation is required.
 * @return Result produced by the operation; see the function implementation for success and error values.
 */
uint32_t app_video_get_buf_size(void)
{
    uint32_t buf_size = camera_video.camera_buf_hes * camera_video.camera_buf_ves * 2;
    return buf_size;
}

/**
 * @brief Perform the video receive video frame operation.
 *
 * Called by the application when this module operation is required.
 * @param video_fd Input or output value used by this operation.
 * @return Result produced by the operation; see the function implementation for success and error values.
 */
static inline esp_err_t video_receive_video_frame(int video_fd)
{
    memset(&camera_video.v4l2_buf, 0, sizeof(camera_video.v4l2_buf));
    camera_video.v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    camera_video.v4l2_buf.memory = camera_video.camera_mem_mode;
    int res = ioctl(video_fd, VIDIOC_DQBUF, &(camera_video.v4l2_buf));
    if (res != 0)
    {
        CAMERA_ERROR("failed to receive video frame");
        return ESP_FAIL;
    }
    return ESP_OK;
}

/**
 * @brief Perform the video operation video frame operation.
 *
 * Called by the application when this module operation is required.
 * @param video_fd Input or output value used by this operation.
 * @return None.
 */
static inline void video_operation_video_frame(int video_fd)
{
    uint8_t buf_index = camera_video.v4l2_buf.index;
    uint8_t *expected_buf = camera_video.camera_buffer[buf_index];
    camera_video.v4l2_buf.m.userptr = (unsigned long)expected_buf;
    camera_video.v4l2_buf.length = camera_video.camera_buf_size;
    camera_video.user_camera_video_frame_operation_cb(
        expected_buf,
        buf_index,
        camera_video.camera_buf_hes,
        camera_video.camera_buf_ves,
        camera_video.camera_buf_size);
}

/**
 * @brief Perform the video free video frame operation.
 *
 * Called by the application when this module operation is required.
 * @param video_fd Input or output value used by this operation.
 * @return Result produced by the operation; see the function implementation for success and error values.
 */
static inline esp_err_t video_free_video_frame(int video_fd)
{
    if (ioctl(video_fd, VIDIOC_QBUF, &(camera_video.v4l2_buf)) != 0)
    {
        CAMERA_ERROR("failed to free video frame");
        return ESP_FAIL;
    }
    return ESP_OK;
}

/**
 * @brief Perform the video stream start operation.
 *
 * Called by the application when this module operation is required.
 * @param video_fd Input or output value used by this operation.
 * @return Result produced by the operation; see the function implementation for success and error values.
 */
static inline esp_err_t video_stream_start(int video_fd)
{
    CAMERA_INFO("Video Stream Start");
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(video_fd, VIDIOC_STREAMON, &type))
    {
        CAMERA_ERROR("failed to start stream");
        return ESP_FAIL;
    }
    struct v4l2_format format = {0};
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(video_fd, VIDIOC_G_FMT, &format) != 0)
    {
        CAMERA_ERROR("get fmt failed");
        return ESP_FAIL;
    }
    return ESP_OK;
}

/**
 * @brief Perform the video stream stop operation.
 *
 * Called when the application releases or stops the related resource.
 * @param video_fd Input or output value used by this operation.
 * @return Result produced by the operation; see the function implementation for success and error values.
 */
static inline esp_err_t video_stream_stop(int video_fd)
{
    CAMERA_INFO("Video Stream Stop");
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(video_fd, VIDIOC_STREAMOFF, &type))
    {
        CAMERA_ERROR("failed to stop stream");
        return ESP_FAIL;
    }
    xEventGroupSetBits(camera_video.video_event_group, VIDEO_TASK_DELETE_DONE);
    return ESP_OK;
}

/**
 * @brief Perform the video stream task operation.
 *
 * Called by the FreeRTOS scheduler after the task is created.
 * @param arg Input or output value used by this operation.
 * @return None.
 */
static void video_stream_task(void *arg)
{
    int video_fd = *((int *)arg);
    esp_err_t err = ESP_OK;
    while (1)
    {
        EventBits_t bits = xEventGroupGetBits(camera_video.video_event_group);
        if (bits & VIDEO_TASK_DELETE)
        {
            xEventGroupClearBits(camera_video.video_event_group, VIDEO_TASK_DELETE);
            CAMERA_INFO("stop stream");
            err = video_stream_stop(video_fd);
            if (err != ESP_OK)
            {
                CAMERA_ERROR("stop stream: [ %s ]", esp_err_to_name(err));
            }
            break;
        }
        if (bits & VIDEO_TASK_DISPLAY_EN)
        {
            err = video_receive_video_frame(video_fd);
            if (err != ESP_OK)
            {
                CAMERA_ERROR("receive video frame: [ %s ]", esp_err_to_name(err));
                vTaskDelay(pdMS_TO_TICKS(5));
                continue;
            }
            video_operation_video_frame(video_fd);
            err = video_free_video_frame(video_fd);
            if (err != ESP_OK)
            {
                CAMERA_ERROR("free video frame: [ %s ]", esp_err_to_name(err));
            }
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(33));
        }
    }
    vTaskDelete(NULL);
}

/**
 * @brief Perform the video stream task start operation.
 *
 * Called by the FreeRTOS scheduler after the task is created.
 * @param video_fd Input or output value used by this operation.
 * @param core_id Input or output value used by this operation.
 * @return Result produced by the operation; see the function implementation for success and error values.
 */
esp_err_t video_stream_task_start(int video_fd, int core_id)
{
    if (camera_video.video_event_group == NULL)
    {
        camera_video.video_event_group = xEventGroupCreate();
    }
    xEventGroupClearBits(camera_video.video_event_group, VIDEO_TASK_DELETE_DONE | VIDEO_TASK_DISPLAY_EN);
    video_stream_start(video_fd);
    BaseType_t result = xTaskCreatePinnedToCore(video_stream_task, "video stream task", 4096, &video_fd, 3, &camera_video.video_stream_task_handle, core_id);
    if (result != pdPASS)
    {
        CAMERA_ERROR("failed to create video stream task");
        goto errout;
    }
    return ESP_OK;
errout:
    video_stream_stop(video_fd);
    return ESP_FAIL;
}

/**
 * @brief Perform the video stream task stop operation.
 *
 * Called by the FreeRTOS scheduler after the task is created.
 * @param video_fd Input or output value used by this operation.
 * @return Result produced by the operation; see the function implementation for success and error values.
 */
esp_err_t video_stream_task_stop(int video_fd)
{
    xEventGroupSetBits(camera_video.video_event_group, VIDEO_TASK_DELETE);
    return ESP_OK;
}

/**
 * @brief Perform the video register frame operation cb operation.
 *
 * Called by the application when this module operation is required.
 * @param operation_cb Input or output value used by this operation.
 * @return Result produced by the operation; see the function implementation for success and error values.
 */
esp_err_t video_register_frame_operation_cb(camera_video_frame_operation_cb_t operation_cb)
{
    camera_video.user_camera_video_frame_operation_cb = operation_cb;
    return ESP_OK;
}

esp_err_t video_register_analysis_frame_cb(camera_frame_analysis_cb_t analysis_cb)
{
    analysis_frame_cb = analysis_cb;
    return ESP_OK;
}

/**
 * @brief Perform the video stream wait stop operation.
 *
 * Called when the application releases or stops the related resource.
 * @return Result produced by the operation; see the function implementation for success and error values.
 */
esp_err_t video_stream_wait_stop(void)
{
    xEventGroupWaitBits(camera_video.video_event_group, VIDEO_TASK_DELETE_DONE, pdTRUE, pdTRUE, portMAX_DELAY);
    CAMERA_INFO("Video Stream Task Stopped Done");
    return ESP_OK;
}

/**
 * @brief Perform the camera video frame operation operation.
 *
 * Called by the application when this module operation is required.
 * @param camera_buf Input or output value used by this operation.
 * @param camera_buf_index Input or output value used by this operation.
 * @param camera_buf_hes Input or output value used by this operation.
 * @param camera_buf_ves Input or output value used by this operation.
 * @param camera_buf_len Input or output value used by this operation.
 * @return None.
 */
static void camera_video_frame_operation(uint8_t *camera_buf, uint8_t camera_buf_index, uint32_t camera_buf_hes, uint32_t camera_buf_ves, size_t camera_buf_len)
{
    EventBits_t bits = xEventGroupGetBits(camera_video.video_event_group);
    if (!(bits & VIDEO_TASK_DISPLAY_EN))
        return;
    esp_cache_msync((void *)camera_buf, camera_buf_len, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    // The camera is capture-only. Avoid copying frames into LVGL so the
    // analysis path does not compete with the primary UI.
    if (analysis_frame_cb != NULL && (++analysis_frame_counter % 3) == 0)
        analysis_frame_cb(camera_buf, camera_buf_hes, camera_buf_ves, camera_video.camera_bytesperline);
}

/**
 * @brief Perform the camera work operation.
 *
 * Called by the application when this module operation is required.
 * @return Result produced by the operation; see the function implementation for success and error values.
 */
int camera_work()
{
    esp_err_t err = ESP_OK;
    size_t cache_line_size = 0;
    camera_video_id = video_open();
    if (camera_video_id < 0)
    {
        CAMERA_ERROR("failed to open camera video device");
        return -1;
    }
    CAMERA_INFO("camera_video_id = %d", camera_video_id);
    const size_t frame_buffer_size = app_video_get_buf_size();
    err = esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &cache_line_size);
    if (err != ESP_OK || cache_line_size == 0)
    {
        CAMERA_ERROR("failed to get PSRAM cache alignment: %s", esp_err_to_name(err));
        close(camera_video_id);
        return -1;
    }
    for (int i = 0; i < 2; i++)
    {
        cam_buffer[i] = (uint8_t *)heap_caps_aligned_alloc(cache_line_size, frame_buffer_size, MALLOC_CAP_SPIRAM);
        if (cam_buffer[i] == NULL)
        {
            CAMERA_ERROR("failed to allocate camera frame buffer %d (%u bytes)", i, (unsigned)frame_buffer_size);
            for (int j = 0; j < i; j++)
            {
                heap_caps_free(cam_buffer[j]);
                cam_buffer[j] = NULL;
            }
            close(camera_video_id);
            return -1;
        }
        cam_buffer_size[i] = frame_buffer_size;
    }
    CAMERA_INFO("frame_buffer_size=%zu", frame_buffer_size);
    err = video_register_frame_operation_cb(camera_video_frame_operation);
    if (err != ESP_OK)
    {
        CAMERA_INFO("register frame operation ERROR");
    }
    err = camera_video_set_bufs(camera_video_id, 2, (const void **)cam_buffer);
    if (err != ESP_OK)
    {
        CAMERA_ERROR("failed to configure camera frame buffers: %s", esp_err_to_name(err));
        return -1;
    }
    err = video_stream_task_start(camera_video_id, 0);
    if (err != ESP_OK)
    {
        CAMERA_ERROR("failed to start camera stream task: %s", esp_err_to_name(err));
        return -1;
    }
    xEventGroupSetBits(camera_video.video_event_group, VIDEO_TASK_DISPLAY_EN);
    return camera_video_id;
}

/**
 * @brief Perform the set camera img display operation.
 *
 * Called by the application when this module operation is required.
 * @param state Input or output value used by this operation.
 * @return None.
 */
void set_camera_img_display(bool state)
{
    if (state)
    {
        if (camera_video.video_event_group)
            xEventGroupSetBits(camera_video.video_event_group, VIDEO_TASK_DISPLAY_EN);
    }
    else
    {
        if (camera_video.video_event_group)
            xEventGroupClearBits(camera_video.video_event_group, VIDEO_TASK_DISPLAY_EN);
    }
}
/*———————————————————————————————————————Functional function end—————————————————————————————————————————*/
