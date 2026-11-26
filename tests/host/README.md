# Host 测试（纯 C）

在仓库根目录执行：

```sh
mkdir -p build/host
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I BSP/Inc BSP/Src/modbus.c tests/host/test_modbus.c -o build/host/test_modbus
./build/host/test_modbus
```

测试包括固定 CRC/FC03 向量、八种功能码、异常码、拒绝部分多写、广播和其他从站过滤。当前新增测试尚未实际运行。
