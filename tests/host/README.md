# Host 测试（纯 C）

在仓库根目录执行：

```sh
mkdir -p build/host
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I BSP/Inc BSP/Src/modbus.c tests/host/test_modbus.c -o build/host/test_modbus
./build/host/test_modbus
```

测试包括固定 CRC/FC03 向量、八种功能码、异常码、拒绝部分多写、广播和其他从站过滤。已在独立 Linux Host 使用 GCC 和 ASan/UBSan 实际执行，输出 `Modbus core vectors PASS`，退出码 0；三个文件的 Git blob SHA 与 b14ad7f 提交一致。仅覆盖纯 C 协议逻辑，不包括 USART/TIM3、ARM 编译或实体硬件。
