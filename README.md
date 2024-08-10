# 推流方式1：使用Linux设备向外UDP推流+Web端转可视化
缺点是需要一个公网服务，优点是不需要内网穿透，更适合广域网方案。

## 摄像头硬件端
支持任意linux设备，这里我使用Debian系统的410棒子/电视盒子/树莓派。
代码见`linux_udp_cam_push.c`。
```
gcc -o udpcam test2.c -lpthread -lv4l2
sudo ./udpcam
```

## 云服务器端
代码见`udpwebserver.py`。
```
pip install Flask opencv-python

python3 udpwebserver.py
```
可使用docker/pm2等进行自动重启等设置。

## Powered by ChatGPT 4o && Thanks to ChatGPT!!



# 推流方式2：使用Linux设备采集USBCAM同时作为服务器
见mjpg-streamer。缺点是需要内网穿透/在同一网段，优点是不需要服务器。
自己魔改了支持灰阶模式，显著减少每帧传输的体积，进一步加速：https://github.com/ywz978020607/mjpg-streamer