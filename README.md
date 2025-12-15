## mymuduo 网络库

本项目的创新点在于：不依赖第三方库，完全自主实现了一个高性能的网络库，并且在原本的基础上，我们应用了 io_uring 技术，进一步提升了网络 I/O 的效率和性能。

io_uring 相关库使用 liburing，liburing 是一个用于简化 Linux 内核 io_uring 接口使用的用户空间库。它提供了一组函数和数据结构，使得开发者能够更方便地利用 io_uring 的高性能异步 I/O 功能。

具体安装liburing的命令如下：

```bash
git clone https://github.com/axboe/liburing.git
cd liburing
make
sudo make install
```

安装完成后，可以通过在编译选项中添加 `-luring` 来链接 liburing 库。