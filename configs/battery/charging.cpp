#include <fstream>
#include <thread>
#include <chrono>
#include <android/log.h>

#define TAG "charging_thermal_daemon"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)

#define T_NODE "/sys/class/power_supply/battery/temp"
#define L_NODE "/sys/class/power_supply/battery/charge_control_limit"
#define FC_NODE "/sys/class/qcom-battery/fastcharge_enable"

#define HYSTERESIS 10

int calculate_target_idx(int t, int mode, int current_idx) {
    int target = 0;

    switch (mode) {
        case 0:
            if (t >= 370) target = 16;
            else if (t >= 350) target = 10;
            else if (t >= 330) target = 5;
            else target = 0;
            break;

        case 1:
            if (t >= 410) target = 16;
            else if (t >= 390) target = 10;
            else if (t >= 370) target = 5;
            else target = 0;
            break;

        case 2:
            if (t >= 400) target = 16;
            else if (t >= 385) target = 10;
            else if (t >= 370) target = 5;
            else target = 0;
            break;

        default:
            target = 0;
            break;
    }

    if (target < current_idx) {
        int drop_allowance = 0;

        if (current_idx == 16)
            drop_allowance = (mode == 2) ? 400 :
                             (mode == 1 ? 410 : 370);
        else if (current_idx == 10)
            drop_allowance = (mode == 2) ? 385 :
                             (mode == 1 ? 390 : 350);
        else if (current_idx == 5)
            drop_allowance = (mode == 2) ? 370 :
                             (mode == 1 ? 370 : 330);

        if (t > (drop_allowance - HYSTERESIS))
            return current_idx;
    }

    return target;
}

int main() {
    int last_idx = -1;
    int last_mode = -1;
    int current_sleep = 5;

    LOGI("Peridot Charging Thermal Daemon started successfully.");

    while (true) {
        int t = 0;
        int fast_charge_mode = 1;

        {
            std::ifstream f_t(T_NODE);
            if (!(f_t >> t)) {
                LOGW("Failed to read battery temperature rail. Retrying...");
                std::this_thread::sleep_for(std::chrono::seconds(5));
                continue;
            }
        }

        {
            std::ifstream f_fc(FC_NODE);
            if (!(f_fc >> fast_charge_mode)) {
                fast_charge_mode = 1;
            }
        }

        int idx = calculate_target_idx(t, fast_charge_mode, last_idx);

        if (idx != last_idx || fast_charge_mode != last_mode) {
            std::ofstream f_l(L_NODE);
            if (f_l.is_open()) {
                f_l << idx;
                f_l.close();

                LOGI("[Status Change] Temp: %d.%d°C | Mode: %d | Scaling Index: %d",
                     t / 10, t % 10, fast_charge_mode, idx);

                last_idx = idx;
                last_mode = fast_charge_mode;
                current_sleep = 2;
            } else {
                LOGW("Write target failure on charge_control_limit sysfs entry.");
            }
        } else {
            current_sleep = 5;
        }

        std::this_thread::sleep_for(std::chrono::seconds(current_sleep));
    }

    return 0;
}
