#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#define KERNEL_INDEX_SMC 2
#define SMC_CMD_READ_BYTES 5
#define SMC_CMD_WRITE_BYTES 6
#define SMC_CMD_READ_INDEX 8
#define SMC_CMD_READ_KEYINFO 9
#define SMC_BYTES 32
#define SMC_FAN_MANUAL_KEY "FS! "

typedef struct {
    uint8_t major;
    uint8_t minor;
    uint8_t build;
    uint8_t reserved;
    uint16_t release;
} SMCVersionData;

typedef struct {
    uint16_t version;
    uint16_t length;
    uint32_t cpuPLimit;
    uint32_t gpuPLimit;
    uint32_t memPLimit;
} SMCPLimitData;

typedef struct {
    uint32_t dataSize;
    uint32_t dataType;
    uint8_t dataAttributes;
} SMCKeyInfoData;

typedef struct {
    uint32_t key;
    SMCVersionData vers;
    SMCPLimitData pLimitData;
    SMCKeyInfoData keyInfo;
    uint8_t result;
    uint8_t status;
    uint8_t data8;
    uint32_t data32;
    uint8_t bytes[SMC_BYTES];
} SMCParamStruct;

typedef struct {
    uint32_t key;
    uint32_t dataType;
    uint32_t dataSize;
    uint8_t bytes[SMC_BYTES];
} SMCVal;

typedef struct {
    io_connect_t conn;
} SMC;

static volatile sig_atomic_t g_stop = 0;

static void usage(const char *argv0)
{
    fprintf(stderr,
            "Usage:\n"
            "  %s status\n"
            "  %s list [prefix]\n"
            "  %s read <KEY>\n"
            "  %s set-target <fan-index> <rpm>\n"
            "  %s auto\n"
            "  %s daemon [--fan N|--all] [--base RPM] [--interval SEC]\n"
            "\n"
            "Default quiet curve: base to 55C, 1900@60C, 2300@70C, 3000@80C, max@90C.\n",
            argv0, argv0, argv0, argv0, argv0, argv0);
}

static uint32_t key_to_u32(const char *key)
{
    return ((uint32_t)(uint8_t)key[0] << 24) |
           ((uint32_t)(uint8_t)key[1] << 16) |
           ((uint32_t)(uint8_t)key[2] << 8) |
           ((uint32_t)(uint8_t)key[3]);
}

static void u32_to_key(uint32_t value, char out[5])
{
    out[0] = (char)((value >> 24) & 0xff);
    out[1] = (char)((value >> 16) & 0xff);
    out[2] = (char)((value >> 8) & 0xff);
    out[3] = (char)(value & 0xff);
    out[4] = '\0';
}

static bool parse_key(const char *s, uint32_t *out)
{
    if (!s || strlen(s) != 4) return false;
    *out = key_to_u32(s);
    return true;
}

static kern_return_t smc_call(SMC *smc, SMCParamStruct *in, SMCParamStruct *out)
{
    size_t inSize = sizeof(*in);
    size_t outSize = sizeof(*out);
    memset(out, 0, sizeof(*out));
    return IOConnectCallStructMethod(smc->conn, KERNEL_INDEX_SMC, in, inSize, out, &outSize);
}

static bool smc_open(SMC *smc)
{
    memset(smc, 0, sizeof(*smc));
    io_service_t service = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("AppleSMC"));
    if (!service) {
        fprintf(stderr, "AppleSMC service not found\n");
        return false;
    }
    kern_return_t kr = IOServiceOpen(service, mach_task_self(), 0, &smc->conn);
    IOObjectRelease(service);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "IOServiceOpen AppleSMC failed: 0x%x\n", kr);
        return false;
    }
    return true;
}

static void smc_close(SMC *smc)
{
    if (smc->conn) IOServiceClose(smc->conn);
    smc->conn = 0;
}

static bool smc_get_key_info(SMC *smc, uint32_t key, SMCKeyInfoData *info)
{
    SMCParamStruct in = {0};
    SMCParamStruct out = {0};
    in.key = key;
    in.data8 = SMC_CMD_READ_KEYINFO;
    kern_return_t kr = smc_call(smc, &in, &out);
    if (kr != KERN_SUCCESS || out.result != 0) return false;
    *info = out.keyInfo;
    return true;
}

static bool smc_read(SMC *smc, const char *keyName, SMCVal *val)
{
    uint32_t key = 0;
    if (!parse_key(keyName, &key)) return false;

    SMCKeyInfoData info = {0};
    if (!smc_get_key_info(smc, key, &info)) return false;
    if (info.dataSize > SMC_BYTES) return false;

    SMCParamStruct in = {0};
    SMCParamStruct out = {0};
    in.key = key;
    in.keyInfo.dataSize = info.dataSize;
    in.data8 = SMC_CMD_READ_BYTES;
    kern_return_t kr = smc_call(smc, &in, &out);
    if (kr != KERN_SUCCESS || out.result != 0) return false;

    memset(val, 0, sizeof(*val));
    val->key = key;
    val->dataType = info.dataType;
    val->dataSize = info.dataSize;
    memcpy(val->bytes, out.bytes, info.dataSize);
    return true;
}

static bool smc_key_at_index(SMC *smc, uint32_t index, char keyName[5])
{
    SMCParamStruct in = {0};
    SMCParamStruct out = {0};
    in.data8 = SMC_CMD_READ_INDEX;
    in.data32 = index;
    kern_return_t kr = smc_call(smc, &in, &out);
    if (kr != KERN_SUCCESS || out.result != 0) return false;
    u32_to_key(out.key, keyName);
    return true;
}

static bool smc_write(SMC *smc, const char *keyName, const uint8_t *bytes, uint32_t size, uint32_t dataType)
{
    uint32_t key = 0;
    if (!parse_key(keyName, &key) || size > SMC_BYTES) return false;

    SMCKeyInfoData info = {0};
    if (!smc_get_key_info(smc, key, &info)) return false;
    if (info.dataSize != size) {
        fprintf(stderr, "%s size mismatch: key size %u, requested %u\n", keyName, info.dataSize, size);
        return false;
    }

    SMCParamStruct in = {0};
    SMCParamStruct out = {0};
    in.key = key;
    in.keyInfo.dataSize = size;
    in.keyInfo.dataType = dataType ? dataType : info.dataType;
    memcpy(in.bytes, bytes, size);
    in.data8 = SMC_CMD_WRITE_BYTES;

    kern_return_t kr = smc_call(smc, &in, &out);
    if (kr != KERN_SUCCESS || out.result != 0) {
        fprintf(stderr, "SMC write %s failed: kr=0x%x result=%u\n", keyName, kr, out.result);
        return false;
    }
    return true;
}

static double decode_sp78(const SMCVal *val)
{
    if (val->dataSize < 2) return NAN;
    int16_t raw = (int16_t)(((uint16_t)val->bytes[0] << 8) | val->bytes[1]);
    return (double)raw / 256.0;
}

static double decode_fpe2(const SMCVal *val)
{
    if (val->dataSize < 2) return NAN;
    uint16_t raw = (uint16_t)(((uint16_t)val->bytes[0] << 8) | val->bytes[1]);
    return (double)raw / 4.0;
}

static uint32_t decode_ui(const SMCVal *val)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < val->dataSize && i < 4; i++) {
        n = (n << 8) | val->bytes[i];
    }
    return n;
}

static uint32_t type_to_u32(const char *type)
{
    return key_to_u32(type);
}

static bool encode_fpe2(double rpm, uint8_t out[2])
{
    if (!isfinite(rpm) || rpm < 0 || rpm > 20000) return false;
    uint16_t raw = (uint16_t)lround(rpm * 4.0);
    out[0] = (uint8_t)((raw >> 8) & 0xff);
    out[1] = (uint8_t)(raw & 0xff);
    return true;
}

static bool read_double_key(SMC *smc, const char *key, double *out)
{
    SMCVal val = {0};
    if (!smc_read(smc, key, &val)) return false;
    char type[5];
    u32_to_key(val.dataType, type);
    if (strcmp(type, "sp78") == 0) {
        *out = decode_sp78(&val);
        return isfinite(*out);
    }
    if (strcmp(type, "fpe2") == 0) {
        *out = decode_fpe2(&val);
        return isfinite(*out);
    }
    return false;
}

static bool read_u_key(SMC *smc, const char *key, uint32_t *out)
{
    SMCVal val = {0};
    if (!smc_read(smc, key, &val)) return false;
    *out = decode_ui(&val);
    return true;
}

static int fan_count(SMC *smc)
{
    uint32_t n = 0;
    if (!read_u_key(smc, "FNum", &n) || n == 0 || n > 16) return 1;
    return (int)n;
}

static bool fan_key(char out[5], int fan, const char *suffix)
{
    if (fan < 0 || fan > 9 || strlen(suffix) != 2) return false;
    snprintf(out, 5, "F%d%s", fan, suffix);
    return true;
}

static double read_fan_rpm(SMC *smc, int fan, const char *suffix)
{
    char key[5];
    double rpm = NAN;
    if (!fan_key(key, fan, suffix)) return NAN;
    if (!read_double_key(smc, key, &rpm)) return NAN;
    return rpm;
}

static bool read_fs_mask(SMC *smc, uint16_t *mask)
{
    SMCVal val = {0};
    if (!smc_read(smc, SMC_FAN_MANUAL_KEY, &val) || val.dataSize < 2) return false;
    *mask = (uint16_t)(((uint16_t)val.bytes[0] << 8) | val.bytes[1]);
    return true;
}

static bool write_fs_mask(SMC *smc, uint16_t mask)
{
    uint8_t bytes[2] = {(uint8_t)((mask >> 8) & 0xff), (uint8_t)(mask & 0xff)};
    return smc_write(smc, SMC_FAN_MANUAL_KEY, bytes, 2, type_to_u32("ui16"));
}

static bool set_fan_auto(SMC *smc, int fan)
{
    if (fan < 0) return write_fs_mask(smc, 0);
    uint16_t mask = 0;
    if (!read_fs_mask(smc, &mask)) return true;
    mask &= (uint16_t)~(1U << fan);
    return write_fs_mask(smc, mask);
}

static bool set_fan_target(SMC *smc, int fan, double rpm)
{
    char key[5];
    if (!fan_key(key, fan, "Tg")) return false;

    double min = read_fan_rpm(smc, fan, "Mn");
    double max = read_fan_rpm(smc, fan, "Mx");
    if (isfinite(min) && rpm < min) rpm = min;
    if (isfinite(max) && rpm > max) rpm = max;

    uint8_t bytes[2];
    if (!encode_fpe2(rpm, bytes)) return false;
    if (!smc_write(smc, key, bytes, 2, type_to_u32("fpe2"))) return false;

    uint16_t mask = 0;
    if (read_fs_mask(smc, &mask)) {
        mask |= (uint16_t)(1U << fan);
        if (!write_fs_mask(smc, mask)) return false;
    }
    return true;
}

static bool read_cpu_temp(SMC *smc, double *outTemp, char outKey[5])
{
    const char *keys[] = {
        "TCXc", "TCXC", "TCMX", "TC0D", "TC0P", "TC0E", "TC0F", "TC0H", "TC0C", "TC1C", "TC2C",
        "TG0D", "TG0P"
    };
    double best = NAN;
    const char *bestKey = NULL;
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        double t = NAN;
        if (read_double_key(smc, keys[i], &t) && t > -20.0 && t < 130.0) {
            if (!isfinite(best) || t > best) {
                best = t;
                bestKey = keys[i];
            }
        }
    }
    if (!isfinite(best)) return false;
    *outTemp = best;
    if (outKey) {
        strncpy(outKey, bestKey ? bestKey : "????", 5);
        outKey[4] = '\0';
    }
    return true;
}

static double curve_target(double temp, double base, double max)
{
    struct Point {
        double temp;
        double rpm;
    };
    struct Point pts[] = {
        {55.0, base},
        {60.0, fmax(base + 150.0, 1900.0)},
        {70.0, fmax(base + 500.0, 2300.0)},
        {80.0, fmax(base + 1000.0, 3000.0)},
        {90.0, max}
    };

    if (temp <= pts[0].temp) return pts[0].rpm;
    for (size_t i = 1; i < sizeof(pts) / sizeof(pts[0]); i++) {
        if (temp <= pts[i].temp) {
            double f = (temp - pts[i - 1].temp) / (pts[i].temp - pts[i - 1].temp);
            return pts[i - 1].rpm + f * (pts[i].rpm - pts[i - 1].rpm);
        }
    }
    return max;
}

static void on_signal(int signo)
{
    (void)signo;
    g_stop = 1;
}

static int command_read(SMC *smc, const char *key)
{
    SMCVal val = {0};
    if (!smc_read(smc, key, &val)) {
        fprintf(stderr, "Cannot read %s\n", key);
        return 1;
    }

    char type[5];
    u32_to_key(val.dataType, type);
    printf("%s type=%s size=%u bytes=", key, type, val.dataSize);
    for (uint32_t i = 0; i < val.dataSize; i++) printf("%02x", val.bytes[i]);
    if (strcmp(type, "sp78") == 0) printf(" value=%.2fC", decode_sp78(&val));
    if (strcmp(type, "fpe2") == 0) printf(" value=%.0frpm", decode_fpe2(&val));
    if (strncmp(type, "ui", 2) == 0 || strcmp(type, "flgs") == 0) printf(" value=%u", decode_ui(&val));
    printf("\n");
    return 0;
}

static int command_status(SMC *smc)
{
    int fans = fan_count(smc);
    uint16_t mask = 0;
    bool hasFsMask = read_fs_mask(smc, &mask);
    double temp = NAN;
    char tempKey[5] = "????";
    if (read_cpu_temp(smc, &temp, tempKey)) {
        printf("Control temp: %.2f C (%s)\n", temp, tempKey);
    } else {
        printf("Control temp: unavailable\n");
    }
    if (hasFsMask) printf("FS! manual mask: 0x%04x\n", mask);
    else printf("FS! manual mask: unavailable; using F*Tg target writes\n");
    for (int fan = 0; fan < fans; fan++) {
        printf("Fan %d: actual=%.0f rpm min=%.0f rpm target=%.0f rpm max=%.0f rpm mode=%s\n",
               fan,
               read_fan_rpm(smc, fan, "Ac"),
               read_fan_rpm(smc, fan, "Mn"),
               read_fan_rpm(smc, fan, "Tg"),
               read_fan_rpm(smc, fan, "Mx"),
               hasFsMask ? ((mask & (1U << fan)) ? "manual" : "auto") : "target");
    }
    const char *temps[] = {"TCXc", "TCXC", "TCMX", "TC0D", "TC0P", "TC0E", "TC0F", "TC0H", "TC0C", "TG0D", "TG0P"};
    for (size_t i = 0; i < sizeof(temps) / sizeof(temps[0]); i++) {
        double t = NAN;
        if (read_double_key(smc, temps[i], &t)) printf("%s=%.2f C\n", temps[i], t);
    }
    return 0;
}

static int command_list(SMC *smc, const char *prefix)
{
    uint32_t count = 0;
    if (!read_u_key(smc, "#KEY", &count) || count == 0 || count > 100000) {
        fprintf(stderr, "Cannot read #KEY\n");
        return 1;
    }

    size_t prefixLen = prefix ? strlen(prefix) : 0;
    for (uint32_t i = 0; i < count; i++) {
        char key[5];
        if (!smc_key_at_index(smc, i, key)) continue;
        if (prefixLen && strncmp(key, prefix, prefixLen) != 0) continue;
        SMCVal val = {0};
        if (smc_read(smc, key, &val)) {
            char type[5];
            u32_to_key(val.dataType, type);
            printf("%s type=%s size=%u bytes=", key, type, val.dataSize);
            for (uint32_t j = 0; j < val.dataSize; j++) printf("%02x", val.bytes[j]);
            if (strcmp(type, "sp78") == 0) printf(" value=%.2fC", decode_sp78(&val));
            if (strcmp(type, "fpe2") == 0) printf(" value=%.0frpm", decode_fpe2(&val));
            if (strncmp(type, "ui", 2) == 0 || strcmp(type, "flgs") == 0) printf(" value=%u", decode_ui(&val));
            printf("\n");
        } else {
            printf("%s unreadable\n", key);
        }
    }
    return 0;
}

static int command_daemon(SMC *smc, int argc, char **argv)
{
    int fan = 0;
    bool allFans = false;
    double base = 1750.0;
    int interval = 5;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--all") == 0) {
            allFans = true;
        } else if (strcmp(argv[i], "--fan") == 0 && i + 1 < argc) {
            fan = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--base") == 0 && i + 1 < argc) {
            base = atof(argv[++i]);
        } else if (strcmp(argv[i], "--interval") == 0 && i + 1 < argc) {
            interval = atoi(argv[++i]);
        } else {
            fprintf(stderr, "Unknown daemon argument: %s\n", argv[i]);
            return 2;
        }
    }
    if (interval < 2) interval = 2;
    if (interval > 60) interval = 60;

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    openlog("smcfanctl", LOG_PID | LOG_CONS, LOG_DAEMON);

    int fans = fan_count(smc);
    if (allFans) fan = -1;
    if (!allFans && (fan < 0 || fan >= fans)) {
        syslog(LOG_ERR, "fan %d is out of range; fan count is %d", fan, fans);
        return 2;
    }

    double lastTargets[16] = {0};
    syslog(LOG_NOTICE, "quiet curve started: fans=%d selected=%s%d base=%.0f interval=%d",
           fans, allFans ? "all" : "", allFans ? 0 : fan, base, interval);

    unsigned tick = 0;
    while (!g_stop) {
        double temp = NAN;
        char tempKey[5] = "????";
        bool ok = read_cpu_temp(smc, &temp, tempKey);
        if (!ok) {
            syslog(LOG_ERR, "temperature unavailable; switching fan control back to auto");
            if (allFans) set_fan_auto(smc, -1);
            else set_fan_auto(smc, fan);
            sleep((unsigned)interval);
            continue;
        }

        int startFan = allFans ? 0 : fan;
        int endFan = allFans ? fans : fan + 1;
        for (int f = startFan; f < endFan; f++) {
            double min = read_fan_rpm(smc, f, "Mn");
            double max = read_fan_rpm(smc, f, "Mx");
            if (!isfinite(min)) min = 1200.0;
            if (!isfinite(max) || max < min) max = 4500.0;

            double b = fmax(base, min);
            double desired = curve_target(temp, b, max);
            if (desired < min) desired = min;
            if (desired > max) desired = max;

            double prev = lastTargets[f] > 0 ? lastTargets[f] : desired;
            if (desired < prev) {
                double drop = (temp < 58.0) ? 180.0 : 90.0;
                desired = fmax(desired, prev - drop);
            }
            if (fabs(desired - prev) < 40.0 && lastTargets[f] > 0) desired = prev;

            bool logNow = lastTargets[f] <= 0 || fabs(desired - lastTargets[f]) >= 40.0 || (tick % 12) == 0;
            if (set_fan_target(smc, f, desired)) {
                lastTargets[f] = desired;
                if (logNow) {
                    syslog(LOG_NOTICE, "temp=%.1fC key=%s fan%d target=%.0frpm actual=%.0frpm",
                           temp, tempKey, f, desired, read_fan_rpm(smc, f, "Ac"));
                }
            } else {
                syslog(LOG_ERR, "failed to set fan%d target %.0frpm", f, desired);
            }
        }

        tick++;
        for (int i = 0; i < interval && !g_stop; i++) sleep(1);
    }

    syslog(LOG_NOTICE, "quiet curve stopped; restoring auto fan mode");
    if (allFans) set_fan_auto(smc, -1);
    else set_fan_auto(smc, fan);
    closelog();
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage(argv[0]);
        return 2;
    }

    SMC smc;
    if (!smc_open(&smc)) return 1;

    int rc = 0;
    if (strcmp(argv[1], "status") == 0) {
        rc = command_status(&smc);
    } else if (strcmp(argv[1], "list") == 0 && argc <= 3) {
        rc = command_list(&smc, argc == 3 ? argv[2] : NULL);
    } else if (strcmp(argv[1], "read") == 0 && argc == 3) {
        rc = command_read(&smc, argv[2]);
    } else if (strcmp(argv[1], "set-target") == 0 && argc == 4) {
        int fan = atoi(argv[2]);
        double rpm = atof(argv[3]);
        rc = set_fan_target(&smc, fan, rpm) ? 0 : 1;
    } else if (strcmp(argv[1], "auto") == 0) {
        rc = set_fan_auto(&smc, -1) ? 0 : 1;
    } else if (strcmp(argv[1], "daemon") == 0) {
        rc = command_daemon(&smc, argc - 2, argv + 2);
    } else {
        usage(argv[0]);
        rc = 2;
    }

    smc_close(&smc);
    return rc;
}
