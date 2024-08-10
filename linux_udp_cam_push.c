#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <sys/mman.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define WIDTH 640
#define HEIGHT 480
#define BUFFER_COUNT 4
#define DEST_IP "127.0.0.1"  // 目标IP地址
#define DEST_PORT 8888  // 目标端口号

struct buffer {
    void   *start;
    size_t length;
};

struct frame_data {
    unsigned char *data;
    size_t size;
    pthread_mutex_t mutex;
};

struct thread_args {
    int fd;
    struct buffer *buffers;
};

int running = 1;
struct frame_data current_frame = { NULL, 0, PTHREAD_MUTEX_INITIALIZER };

void *capture_thread(void *arg) {
    struct thread_args *args = (struct thread_args *)arg;
    int fd = args->fd;
    struct buffer *buffers = args->buffers;
    struct v4l2_buffer buf;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    while (running) {
        memset(&buf, 0, sizeof(buf));
        buf.type = type;
        buf.memory = V4L2_MEMORY_MMAP;

        if (ioctl(fd, VIDIOC_DQBUF, &buf) == -1) {
            perror("Failed to dequeue buffer");
            break;
        }

        pthread_mutex_lock(&current_frame.mutex);

        // 先释放旧的 frame_data 内存，避免内存泄漏
        if (current_frame.data) {
            free(current_frame.data);
        }

        // 分配新内存并复制图像数据
        current_frame.size = buf.bytesused;
        current_frame.data = malloc(buf.bytesused);
        if (current_frame.data == NULL) {
            perror("Failed to allocate memory for frame data");
            pthread_mutex_unlock(&current_frame.mutex);
            break;
        }
        memcpy(current_frame.data, buffers[buf.index].start, buf.bytesused);

        pthread_mutex_unlock(&current_frame.mutex);

        // 重新将缓冲区放回队列
        if (ioctl(fd, VIDIOC_QBUF, &buf) == -1) {
            perror("Failed to requeue buffer");
            break;
        }
    }

    return NULL;
}

void *udp_send_thread(void *arg) {
    int sockfd;
    struct sockaddr_in dest_addr;

    // 创建 UDP 套接字
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1) {
        perror("Failed to create UDP socket");
        return NULL;
    }

    // 设置目标地址
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(DEST_PORT);
    inet_pton(AF_INET, DEST_IP, &dest_addr.sin_addr);

    while (running) {
        pthread_mutex_lock(&current_frame.mutex);
        if (current_frame.data && current_frame.size > 0) {
            if (sendto(sockfd, current_frame.data, current_frame.size, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) == -1) {
                perror("Failed to send frame");
            }
        }
        pthread_mutex_unlock(&current_frame.mutex);

        usleep(10000);  // 可以根据需要调整发送间隔
    }

    close(sockfd);
    return NULL;
}

int main() {
    int fd;
    struct v4l2_capability cap;
    struct v4l2_format fmt;
    struct v4l2_requestbuffers req;
    struct v4l2_buffer buf;
    struct buffer *buffers;
    pthread_t capture_tid, udp_tid;
    int i;

    // 打开视频设备
    fd = open("/dev/video1", O_RDWR);
    if (fd == -1) {
        perror("Failed to open video device");
        return 1;
    }

    // 查询设备能力
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == -1) {
        perror("Failed to query device capabilities");
        close(fd);
        return 1;
    }

    // 检查设备是否支持视频捕捉
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "The device does not support video capture.\n");
        close(fd);
        return 1;
    }

    // 设置视频格式为 MJPEG
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = WIDTH;
    fmt.fmt.pix.height = HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;

    if (ioctl(fd, VIDIOC_S_FMT, &fmt) == -1) {
        perror("Failed to set format");
        close(fd);
        return 1;
    }

    // 请求缓冲区
    memset(&req, 0, sizeof(req));
    req.count = BUFFER_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd, VIDIOC_REQBUFS, &req) == -1) {
        perror("Failed to request buffers");
        close(fd);
        return 1;
    }

    // 分配缓冲区
    buffers = calloc(req.count, sizeof(*buffers));
    if (!buffers) {
        perror("Failed to allocate buffer structures");
        close(fd);
        return 1;
    }

    for (i = 0; i < req.count; i++) {
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) == -1) {
            perror("Failed to query buffer");
            close(fd);
            return 1;
        }

        buffers[i].length = buf.length;
        buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);

        if (buffers[i].start == MAP_FAILED) {
            perror("Failed to map buffer");
            close(fd);
            return 1;
        }
    }

    // 启动视频流
    for (i = 0; i < req.count; i++) {
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (ioctl(fd, VIDIOC_QBUF, &buf) == -1) {
            perror("Failed to queue buffer");
            close(fd);
            return 1;
        }
    }

    if (ioctl(fd, VIDIOC_STREAMON, &fmt.type) == -1) {
        perror("Failed to start capture");
        close(fd);
        return 1;
    }

    // 初始化线程参数
    struct thread_args args = {
        .fd = fd,
        .buffers = buffers
    };

    // 创建捕获线程和UDP发送线程
    pthread_create(&capture_tid, NULL, capture_thread, &args);
    pthread_create(&udp_tid, NULL, udp_send_thread, NULL);

    // 等待线程完成
    pthread_join(capture_tid, NULL);
    pthread_join(udp_tid, NULL);

    // 停止视频流
    if (ioctl(fd, VIDIOC_STREAMOFF, &fmt.type) == -1) {
        perror("Failed to stop capture");
    }

    // 释放资源
    for (i = 0; i < req.count; i++) {
        if (buffers[i].start && buffers[i].start != MAP_FAILED) {
            munmap(buffers[i].start, buffers[i].length);
        }
    }
    free(buffers);
    close(fd);

    return 0;
}
