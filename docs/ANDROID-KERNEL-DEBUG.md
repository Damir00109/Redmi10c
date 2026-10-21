# Андроид-ядро с полным логированием для реверса

Цель: снять вендорскую последовательность инициализации OV02B1B (`0x3c`),
которую мы не смогли достать из Chromatix, ptrace и памяти HAL.

Ключевая идея: **логирует не HAL, а ядро**. Драйвер `cam_cci`/`cam_sensor`
физически выполняет каждую I2C-запись — если включить его debug, он печатает
`addr=value` в dmesg. Это и есть init-таблица в чистом виде.

Имена Kconfig ниже — для дерева `camera-kernel` (techpack/camera) от Qualcomm;
в старых деревьях (`drivers/media/platform/msm/camera_v2`) они называются
`CONFIG_MSM_CAMERA*` — проверять по `grep -r "config CAM_DEBUG" .`.

## 1. Самое важное — камера

```
CONFIG_CAM_DEBUG=y              # база: включает cam_debug уровни и debugfs
CONFIG_CAM_DEBUG_MODULE=y       # логи по модулям (cci, sensor, isp, csid...)
CONFIG_CAM_DEBUG_INFO=y         # info-уровень
CONFIG_CAM_DEBUG_MEM=y          # аллокации/мапы (полезно для CSI-буферов)
CONFIG_CAM_DEBUG_EVENT=y        # события (stream on/off, SOF)
CONFIG_CAM_DEBUG_SYNC=y         # синхронизация запросов
CONFIG_CAM_DEBUG_CRM=y          # request manager
CONFIG_CAM_DEBUG_TIMEOUT=y      # таймауты
```

Для старых деревьев (camera_v2):

```
CONFIG_MSM_CAMERA=y
CONFIG_MSM_CAMERA_DEBUG=y
CONFIG_MSM_CCI=y
```

**Что это даёт:** `/sys/kernel/debug/camera/` и печать каждой I2C-транзакции
через CCI с модульным фильтром.

## 2. Логирование ядра в целом

```
CONFIG_PRINTK=y
CONFIG_LOG_BUF_SHIFT=21          # 2 МБ лог-буфера вместо 17 (128 КБ)
CONFIG_LOG_CPU_MAX_BUF_SHIFT=15
CONFIG_DYNAMIC_DEBUG=y           # включать debug по файлу/функции на ходу
CONFIG_DEBUG_FS=y
CONFIG_DEBUG_FS_ALLOW_ALL=y
CONFIG_KALLSYMS=y
CONFIG_KALLSYMS_ALL=y            # имена всех символов в трейсах
CONFIG_MAGIC_SYSRQ=y             # sysrq для дампа при зависании
CONFIG_PSTORE=y
CONFIG_PSTORE_RAM=y              # ramoops — лог переживает ребут
CONFIG_PSTORE_CONSOLE=y
CONFIG_PSTORE_PMSG=y
```

## 3. Трассировка (ftrace, kprobes, uprobes)

```
CONFIG_FTRACE=y
CONFIG_FUNCTION_TRACER=y
CONFIG_FUNCTION_GRAPH_TRACER=y
CONFIG_DYNAMIC_FTRACE=y
CONFIG_STACK_TRACER=y
CONFIG_KPROBES=y
CONFIG_KPROBE_EVENTS=y           # трассировка аргументов функций
CONFIG_UPROBES=y
CONFIG_UPROBE_EVENTS=y           # трассировка userspace (HAL) по символам
CONFIG_FTRACE_SYSCALLS=y
CONFIG_ENABLE_DEFAULT_TRACERS=y
CONFIG_BPF_SYSCALL=y
CONFIG_BPF_EVENTS=y
CONFIG_TRACEPOINTS=y
```

**Зачем uprobes:** можно повесить probe на функцию HAL (например,
`SensorInit`) и увидеть аргументы — то, чего не дал ptrace.

## 4. Шины, GPIO, питание

```
CONFIG_I2C_CHARDEV=y             # /dev/i2c-N — ручной доступ к сенсору
CONFIG_I2C_DEBUG_CORE=y
CONFIG_I2C_DEBUG_ALGO=y
CONFIG_DEBUG_GPIO=y              # gpio debugfs
CONFIG_GPIO_SYSFS=y
CONFIG_PINCTRL_DEBUG=y
CONFIG_REGULATOR_DEBUG=y
CONFIG_DEBUG_REGULATOR=y
CONFIG_PM_DEBUG=y
CONFIG_PM_ADVANCED_DEBUG=y
```

Плюс regmap debugfs (появляется сам при `CONFIG_DEBUG_FS`):
`/sys/kernel/debug/regmap/*` — видно все регистровые операции драйверов.

## 5. Отладочная информация и символы

```
CONFIG_DEBUG_INFO=y
CONFIG_DEBUG_INFO_DWARF4=y
CONFIG_FRAME_POINTER=y
CONFIG_DEBUG_KERNEL=y
CONFIG_DEBUG_LIST=y
CONFIG_DEBUG_SG=y
CONFIG_DMA_API_DEBUG=y
```

`DEBUG_INFO` нужен, чтобы ftrace/kprobes показывали имена функций и
аргументы, а не адреса.

## 6. Отключить то, что мешает

```
# CONFIG_MODULE_SIG_FORCE is not set
# CONFIG_SECURITY_SELINUX_BOOTPARAM_VALUE  -> 0 (permissive)
# CONFIG_DM_VERITY_FEC ... — verity лучше отключить в cmdline
```

## 7. Параметры командной строки ядра

```
androidboot.selinux=permissive
cam_debug=0x1f                   # маска уровней (err/warn/info/dbg)
cam_debug_mdl=0x...              # маска модулей: cci | sensor | csid | isp
```

Точные имена параметров зависят от дерева — проверить:

```sh
grep -rn "cam_debug_mdl\|module_param" techpack/camera/drivers/cam_utils/
```

В `camera-kernel` это `cam_debug_mdl` (модульная маска) и `cam_debug`
(уровни). Значение маски смотреть в `cam_debug.h`:
`CAM_DEBUG_MDL_CCI`, `CAM_DEBUG_MDL_SENSOR` и т.д.

## 8. Userspace

```sh
adb root
adb shell setprop persist.vendor.camera.debug 1
adb shell setprop vendor.camera.debug.mask 0x1f
adb shell "echo 1 > /sys/kernel/debug/camera/..."
```

Плюс полезно:
- `adb shell dmesg -w` (или `cat /dev/kmsg`)
- `adb shell cat /sys/kernel/debug/regmap/*/registers`
- отключить verity на boot/vendor (иначе не поменять модули)

## 9. Как снимать init-последовательность

1. Загрузиться с новым ядром, `cam_debug_mdl` с маской CCI+sensor.
2. Открыть камеру ID 2 (портрет/глубина) в стоковом приложении.
3. Снять лог:
   ```sh
   adb shell dmesg -c > /dev/null      # очистить
   # ... открыть камеру ...
   adb shell dmesg > /tmp/init.log
   ```
4. Искать строки вида:
   ```
   cam_cci: cci_write: addr=0x3c reg=0x0100 val=0x00
   cam_sensor: i2c write addr=0x3c reg=0x300a val=0x00
   ```
5. Это и есть таблица. Сравнить с нашей (92 записи) — найти недостающее.

**Альтернатива без логов:** `/sys/kernel/debug/regmap/1-003c/registers` сразу
после старта стрима — там текущее состояние всех регистров сенсора.

## 10. Если логов камеры всё равно мало

Дополнительно:
- `CONFIG_DEBUG_KMEMLEAK=y` — если подозрение на утечки (тяжёлый)
- `CONFIG_PROVE_LOCKING=y` — если зависания (очень тяжёлый, только для точечной отладки)
- `CONFIG_ARM_SMMU_DEBUG`/IOMMU debug — если нет кадров из-за маппинга

## Итоговый минимум

Если времени мало, достаточно этого:

```
CONFIG_CAM_DEBUG=y
CONFIG_CAM_DEBUG_MODULE=y
CONFIG_DEBUG_FS=y
CONFIG_DYNAMIC_DEBUG=y
CONFIG_LOG_BUF_SHIFT=21
CONFIG_DEBUG_INFO=y
CONFIG_KALLSYMS_ALL=y
CONFIG_I2C_CHARDEV=y
CONFIG_PSTORE_RAM=y
```
