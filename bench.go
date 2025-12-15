package main

import (
    "fmt"
    "net"
    "sync/atomic"
    "time"
)

const (
    serverAddr = "127.0.0.1:8000"
    connNum    = 10000   // 并发连接数
    msgSize    = 1024  // 消息大小 4KB
)

var (
    totalQPS int64
)

func main() {
    fmt.Printf("Start benchmarking %s, conns=%d, msgSize=%d\n", serverAddr, connNum, msgSize)
    
    msg := make([]byte, msgSize)
    for i := 0; i < len(msg); i++ {
        msg[i] = 'a'
    }

    for i := 0; i < connNum; i++ {
        go func() {
            conn, err := net.Dial("tcp", serverAddr)
            if err != nil {
                fmt.Println("Dial error:", err)
                return
            }
            defer conn.Close()

            buf := make([]byte, 4096)
            for {
                _, err := conn.Write(msg)
                if err != nil {
                    return
                }
                _, err = conn.Read(buf)
                if err != nil {
                    return
                }
                atomic.AddInt64(&totalQPS, 1)
            }
        }()
    }

    // 每秒打印一次 QPS
    ticker := time.NewTicker(time.Second)
    for range ticker.C {
        qps := atomic.SwapInt64(&totalQPS, 0)
        fmt.Printf("QPS: %d\n", qps)
    }
}