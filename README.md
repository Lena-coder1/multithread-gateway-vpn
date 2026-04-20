# Multithreaded Gateway VPN

## Demo Video 
[![Watch the video](https://www.youtube.com/watch?v=Rf12zO_mLTM)](https://www.youtube.com/watch?v=Rf12zO_mLTM)

## Introduction

The Multithreaded Gateway VPN is a network application that acts as an intermediary between clients and external servers. It functions as a proxy-based VPN gateway that securely forwards traffic using SSL/TLS. The system is designed to handle multiple client connections simultaneously by leveraging multithreading, ensuring efficient and scalable communication.

To build the multithreaded gateway vpn , i used socket programming, HTTPS tunneling using the CONNECT method, and secure communication using OpenSSL. this project helped me understand the how vpns and proxy servers manage encrypted web traffic.

---

## How It Works

1. **Client Connection**

   * A client using chrome broswer connects to the gateway using a proxy configuration (`127.0.0.1:8080`).

2. **Request Handling**

   * The gateway receives an HTTP `CONNECT` request, which is used for HTTPS tunneling.
   * Example: `CONNECT www.google.com:443 HTTP/1.1`

3. **Multithreading**

   * Each incoming client connection is handled in a separate thread.
   * This allows multiple clients to connect and communicate at the same time without blocking each other.

4. **Secure Communication**

   * The gateway establishes an SSL/TLS connection with the target server using OpenSSL.
   * For testing purposes, certificate verification was disabled to allow connections to succeed   without validating the server’s certificate.
   * While encryption is still maintained, this reduces security and would not be appropriate for production use.

5. **Traffic Forwarding**

   * Data is forwarded between the client and the destination server.
   * The gateway acts as a tunnel, relaying encrypted traffic without modifying it.

---

## How to Run the Program

### 1. Navigate to the Project Directory

```MSYS2 MINGW64 Shell
cd "C:\Users\lenam\desktop\cPAN226\multithread-gateway-vpn"
```

### 2. Compile the Program


```MSYS2 MINGW64 Shell
gcc vpnserver.c -o vpnserver.exe -I/mingw64/include -L/mingw64/lib -lssl -lcrypto -lws2_32 -lpthread
gcc vpnclient.c -o vpnclient.exe -I/mingw64/include -L/mingw64/lib -lssl -lcrypto -lws2_32 -lpthread
```

### 3. Run the Server

``` MSYS2 MINGW64 Shell
./vpnserver
./vpnclient.exe 127.0.0.1
```

You should see output such as:

* `New VPN client connected`
* `SSL handshake successful`
* `Forwarding to: (target server)`

---

### 4. Launch Chrome with Proxy

In PowerShell:

```powershell
 & "C:\Program Files\Google\Chrome\Application\chrome.exe" --proxy-server="http://127.0.0.1:8888"
```

---

### 5. Test the Connection

Open a website such as:

```
https://google.com
```

If the server logs show:

* `CONNECT ...:443`
* `Forwarding to ...`
* `SSL handshake successful`

Then the VPN gateway is working correctly.

---


