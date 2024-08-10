import cv2
import numpy as np
import socket
from flask import Flask, Response

app = Flask(__name__)

# 设置UDP服务器的IP和端口
UDP_IP = "0.0.0.0"
UDP_PORT = 8888

# 初始化socket
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((UDP_IP, UDP_PORT))

frame_data = None

@app.route('/')
def index():
    """视频流主页"""
    return "Video Streaming Server is running. Access /video_feed to see the stream."

def gen():
    """视频流生成函数"""
    global frame_data
    while True:
        if frame_data is not None:
            # 将frame_data转换为OpenCV图像
            np_data = np.frombuffer(frame_data, dtype=np.uint8)
            frame = cv2.imdecode(np_data, cv2.IMREAD_COLOR)
            if frame is not None:
                # 将图像编码为JPEG格式
                ret, jpeg = cv2.imencode('.jpg', frame)
                if ret:
                    # 生成HTTP响应
                    yield (b'--frame\r\n'
                           b'Content-Type: image/jpeg\r\n\r\n' + jpeg.tobytes() + b'\r\n\r\n')

@app.route('/video_feed')
def video_feed():
    """视频流路由"""
    return Response(gen(),
                    mimetype='multipart/x-mixed-replace; boundary=frame')

def udp_receiver():
    global frame_data
    while True:
        # 接收数据
        data, addr = sock.recvfrom(65535)  # 假设每帧数据不超过65535字节
        frame_data = data

if __name__ == '__main__':
    # 启动UDP接收线程
    from threading import Thread
    udp_thread = Thread(target=udp_receiver)
    udp_thread.daemon = True
    udp_thread.start()

    # 启动Flask服务器
    app.run(host='0.0.0.0', port=8080, debug=False)
