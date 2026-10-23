/*
 * SPDX-License-Identifier: BSD-3-Clause AND GPL-3.0-only
 *
 * Copyright (c) 2025 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 *
 * Inherited BSD-licensed material remains BSD-3-Clause.
 * New original contributions by xiphonics, inc. are GPL-3.0-only.
 * Previously granted BSD permissions are preserved. See LICENSING.md.
 */
#include "charger.h"
#include "FreeRTOS.h"
#include "System/Console/Trace.h"
#include "chargerWatchdog.h"
#include "gpio.h"
#include "i2c.h"
#include "platform.h"
#include "rtc.h"
#include "task.h"
#include "timers.h"
#include "tlv320aic3204.h"

#include <cstdint>
#include <cstdio>
#include <stdlib.h>

#define BQ25601_I2C_ADDR 0x6B
#define BQ25601_INPUT_REG 0x00
#define BQ25601_CHARGING_REG 0x01
#define BQ25601_CURRENT_REG 0x02
#define BQ25601_TIMER_REG 0x05
#define BQ25601_SHIPMODE_REG 0x07
#define BQ25601_STATUS_REG 0x08
#define BQ25601_FAULT_REG 0x09
#define BQ25601_INT_REG 0x0A
#define BQ25601_RESET_REG 0x0B
#define BQ25601_REG_RESET_BIT 7
#define BQ25601_WD_RST_BIT 6
#define BQ25601_OTG_CONFIG_BIT 5
#define BQ25601_PG_STAT 1 << 2
#define BQ25601_OTG_POLL_INTERVAL_MS 25
#define BQ25601_OTG_STARTUP_TIMEOUT_MS 2000
#define CHARGER_WATCHDOG_RESET_PERIOD_MS 15000
#define BIT_CHG_CONFIG 4
#define BIT_VBUS_GD 7
#define CHRG_STAT_POS 3
#define CHRG_STAT_MASK (0x3 << CHRG_STAT_POS)
#define CHRG_STAT_NOT 0x0
#define CHRG_STAT_PRE 0x1
#define CHRG_STAT_FAST 0x2
#define CHRG_STAT_DONE 0x3
#define CHRG_FAULT_POS 4
#define CHRG_FAULT_MASK (0x3 << CHRG_FAULT_POS)
#define CHRG_FAULT_NORMAL 0x0
#define CHRG_FAULT_INP 0x1
#define CHRG_FAULT_TERMAL 0x2
#define CHRG_FAULT_TIMER 0x3

static TimerHandle_t chargerWatchdogTimer = NULL;
static StaticTimer_t chargerWatchdogTimerBuffer;

static bool resetChargerWatchdog(void);

struct HostPowerPollStatus {
  uint8_t reg01;
  uint8_t reg08;
  uint8_t reg09Latched;
  uint8_t reg09;
  uint8_t reg0a;
};

static bool readChargerRegister(uint8_t reg, uint8_t &value) {
  const HAL_StatusTypeDef status =
      HAL_I2C_Mem_Read(&hi2c4, BQ25601_I2C_ADDR << 1, reg, I2C_MEMADD_SIZE_8BIT,
                       &value, 1, I2C_TRANSFER_TIMEOUT_MS);
  if (status != HAL_OK) {
    Trace::Error("charger read reg 0x%02X failed: %i", reg, status);
    return false;
  }

  return true;
}

static const char *chargeFaultName(uint8_t value) {
  switch (value) {
  case 0x00:
    return "ok";
  case 0x01:
    return "input";
  case 0x02:
    return "therm";
  case 0x03:
    return "timer";
  default:
    return "?";
  }
}

static const char *ntcFaultName(uint8_t value) {
  switch (value) {
  case 0x00:
    return "ok";
  case 0x02:
    return "warm";
  case 0x03:
    return "cool";
  case 0x05:
    return "cold";
  case 0x06:
    return "hot";
  default:
    return "rsv";
  }
}

static void hostPowerDelayMs(uint32_t delayMs) {
  if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
    vTaskDelay(pdMS_TO_TICKS(delayMs));
    return;
  }

  HAL_Delay(delayMs);
}

static bool readHostPowerPollStatus(HostPowerPollStatus &status) {
  return readChargerRegister(BQ25601_CHARGING_REG, status.reg01) &&
         readChargerRegister(BQ25601_STATUS_REG, status.reg08) &&
         readChargerRegister(BQ25601_FAULT_REG, status.reg09Latched) &&
         readChargerRegister(BQ25601_FAULT_REG, status.reg09) &&
         readChargerRegister(BQ25601_INT_REG, status.reg0a);
}

static void traceHostPowerCause(uint8_t reg00, uint8_t reg01, uint8_t reg02,
                                uint8_t reg07, uint8_t reg08, uint8_t reg09,
                                uint8_t reg0a) {
  if (chargerInputHighImpedanceFromReg00(reg00)) {
    Trace::Error("CHARGER host cause: HIZ blocks OTG");
  } else if (chargerBatfetDisabledFromReg07(reg07)) {
    Trace::Error("CHARGER host cause: BATFET disabled");
  } else if (!chargerOtgConfigFromReg01(reg01)) {
    Trace::Error("CHARGER host cause: OTG bit dropped");
  } else if (chargerBoostFaultFromReg09(reg09)) {
    Trace::Error("CHARGER host cause: boost fault");
  } else if (chargerNtcFaultFromReg09(reg09) != 0u) {
    Trace::Error("CHARGER host cause: NTC fault %s",
                 ntcFaultName(chargerNtcFaultFromReg09(reg09)));
  } else if (chargerAcOverVoltageFromReg0A(reg0a)) {
    Trace::Error("CHARGER host cause: AC overvoltage");
  } else if (chargerVbusGoodFromReg0A(reg0a)) {
    Trace::Error("CHARGER host cause: VBUS already good");
  } else if (!chargerVbusStatusIsOtg(reg08)) {
    Trace::Error("CHARGER host cause: OTG never became active");
  } else {
    Trace::Error("CHARGER host cause: unknown");
  }

  Trace::Error(
      "CHARGER host regs R00=0x%02X R01=0x%02X R02=0x%02X R07=0x%02X "
      "R08=0x%02X R09=0x%02X R0A=0x%02X hiz=%u otg=%u chg=%u iin=%u "
      "bstlim=%u pg=%u vsys=%u boost_fault=%u chg_fault=%s ntc=%s",
      reg00, reg01, reg02, reg07, reg08, reg09, reg0a,
      chargerInputHighImpedanceFromReg00(reg00) ? 1u : 0u,
      chargerOtgConfigFromReg01(reg01) ? 1u : 0u,
      chargerChargeConfigFromReg01(reg01) ? 1u : 0u,
      static_cast<unsigned>(chargerInputCurrentLimitMaFromReg00(reg00)),
      chargerBoostLimitHighFromReg02(reg02) ? 1u : 0u,
      chargerPowerGoodFromReg08(reg08) ? 1u : 0u,
      chargerVsysMinRegulationFromReg08(reg08) ? 1u : 0u,
      chargerBoostFaultFromReg09(reg09) ? 1u : 0u,
      chargeFaultName(chargerChargeFaultFromReg09(reg09)),
      ntcFaultName(chargerNtcFaultFromReg09(reg09)));
}

static bool resetChargerWatchdog(void) {
  uint8_t regValue = 0;
  if (!readChargerRegister(BQ25601_CHARGING_REG, regValue)) {
    Trace::Error("Charger watchdog reset read failed");
    return false;
  }

  regValue |= (1u << BQ25601_WD_RST_BIT);
  const HAL_StatusTypeDef status = HAL_I2C_Mem_Write(
      &hi2c4, BQ25601_I2C_ADDR << 1, BQ25601_CHARGING_REG, I2C_MEMADD_SIZE_8BIT,
      &regValue, 1, I2C_TRANSFER_TIMEOUT_MS);
  if (status != HAL_OK) {
    Trace::Error("Charger watchdog reset write failed: %i", status);
    return false;
  }

  Trace::Log("CHARGER", "BQ25601 watchdog reset");
  return true;
}

void logHostPowerDiagnostics(const char *reason) {
  uint8_t reg00 = 0;
  uint8_t reg01 = 0;
  uint8_t reg02 = 0;
  uint8_t reg07 = 0;
  uint8_t reg08 = 0;
  uint8_t reg09Latched = 0;
  uint8_t reg09 = 0;
  uint8_t reg0a = 0;

  if (!readChargerRegister(BQ25601_INPUT_REG, reg00) ||
      !readChargerRegister(BQ25601_CHARGING_REG, reg01) ||
      !readChargerRegister(BQ25601_CURRENT_REG, reg02) ||
      !readChargerRegister(BQ25601_SHIPMODE_REG, reg07) ||
      !readChargerRegister(BQ25601_STATUS_REG, reg08) ||
      !readChargerRegister(BQ25601_FAULT_REG, reg09Latched) ||
      !readChargerRegister(BQ25601_FAULT_REG, reg09) ||
      !readChargerRegister(BQ25601_INT_REG, reg0a)) {
    Trace::Error("CHARGER host diag %s unavailable",
                 reason != nullptr ? reason : "unknown");
    return;
  }

  const uint8_t vbusStat = static_cast<uint8_t>((reg08 >> 5) & 0x07u);
  const bool pgStat = (reg08 & BQ25601_PG_STAT) != 0u;
  const bool watchdogFault = (reg09 & (1u << 7)) != 0u;
  const bool boostFault = (reg09 & (1u << 6)) != 0u;
  const uint8_t chargeFault =
      static_cast<uint8_t>((reg09 >> CHRG_FAULT_POS) & 0x03u);

  Trace::Log("CHARGER",
             "Host diag %s: REG01=0x%02X REG08=0x%02X REG09=0x%02X "
             "otg=%u pg=%u vbus_stat=%u boost_fault=%u watchdog_fault=%u "
             "charge_fault=%u",
             reason != nullptr ? reason : "unknown", reg01, reg08, reg09,
             (reg01 >> BQ25601_OTG_CONFIG_BIT) & 0x01u, pgStat ? 1u : 0u,
             vbusStat, boostFault ? 1u : 0u, watchdogFault ? 1u : 0u,
             chargeFault);
  if (reg09Latched != reg09) {
    Trace::Log("CHARGER", "Host diag %s latched REG09=0x%02X",
               reason != nullptr ? reason : "unknown", reg09Latched);
  }
  if (!chargerVbusStatusIsOtg(reg08)) {
    traceHostPowerCause(reg00, reg01, reg02, reg07, reg08, reg09, reg0a);
  }
}

static void chargerWatchdogTimerHandler(TimerHandle_t xTimer) {
  (void)xTimer;

  if (!resetChargerWatchdog()) {
    Trace::Error("Charger watchdog refresh failed");
  }
}

static bool startChargerWatchdogTimer(void) {
  if (chargerWatchdogTimer == NULL) {
    chargerWatchdogTimer = xTimerCreateStatic(
        "ChgWdt", pdMS_TO_TICKS(CHARGER_WATCHDOG_RESET_PERIOD_MS), pdTRUE,
        (void *)0, chargerWatchdogTimerHandler, &chargerWatchdogTimerBuffer);
    if (chargerWatchdogTimer == NULL) {
      Trace::Error("Failed to create charger watchdog timer");
      return false;
    }
  }

  if (xTimerIsTimerActive(chargerWatchdogTimer) == pdFALSE) {
    if (xTimerStart(chargerWatchdogTimer, pdMS_TO_TICKS(100)) != pdPASS) {
      Trace::Error("Failed to start charger watchdog timer");
      return false;
    }
  }

  return true;
}

// Note: NOT_CHARGING enum is specifically made to be 0 (ie. false)
ChargingStatus getChargingStatus() {
  uint8_t reg_value = 0;
  HAL_StatusTypeDef status = HAL_I2C_Mem_Read(
      &hi2c4, BQ25601_I2C_ADDR << 1, BQ25601_STATUS_REG, I2C_MEMADD_SIZE_8BIT,
      &reg_value, 1, I2C_TRANSFER_TIMEOUT_MS);

  if (status != HAL_OK) {
    Trace::Error("GetCharginStatus: i2c read error: %i", status);
    return NOT_CHARGING;
  }

  // Extract CHG_STAT
  uint8_t chg_stat = (reg_value & CHRG_STAT_MASK) >> CHRG_STAT_POS;

  switch (chg_stat) {
  case 0x00:
    return NOT_CHARGING;
  case 0x01:
    return PRE_CHARGE;
  case 0x02:
    return FAST_CHARGE;
  case 0x03:
    return CHARGE_DONE;
  }
  return NOT_CHARGING; // keep compiler warnings happy
}

void powerOff() {
  // Early boot fatal screens can power down before audio init runs.
  const bool codecInitialized = tlv320_is_initialized();
  if (codecInitialized) {
    tlv320_mute();
  }

  // Disable wake-up timer before reconfiguring
  HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);

  // Clear flags wake-up timer and power related flags
  __HAL_RTC_WAKEUPTIMER_CLEAR_FLAG(&hrtc, RTC_FLAG_WUTF);

  PWR->WKUPCR = 0xFFFFFFFFU;
  __HAL_PWR_CLEAR_FLAG(PWR_FLAG_SB);
  __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);

  // if powered on only shutdown processor
  uint8_t reg_value = 0;
  HAL_StatusTypeDef status = HAL_I2C_Mem_Read(
      &hi2c4, BQ25601_I2C_ADDR << 1, BQ25601_STATUS_REG, I2C_MEMADD_SIZE_8BIT,
      &reg_value, 1, I2C_TRANSFER_TIMEOUT_MS);

  bool powerGood = false;
  if (status != HAL_OK) {
    Trace::Error("PowerOff: i2c write error: %i", status);
    // Assume power is present on read failure so we don't drop to ship mode
    // just because of a transient I2C issue.
    powerGood = true;
  } else {
    powerGood = (reg_value & BQ25601_PG_STAT);
  }

  if (powerGood) {
    // TODO: we could further optimize and go to deep sleep if battery is full
    Trace::Log("POWEROFF", "Sleep");

    HAL_GPIO_DeInit(POWER_GPIO_Port, POWER_Pin);
    HAL_PWREx_DisableWakeUpPin(PWR_WAKEUP_PIN2);
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU); // always clear before enabling again
    PWREx_WakeupPinTypeDef sPinParams = {.WakeUpPin = PWR_WAKEUP_PIN2,
                                         .PinPolarity = PWR_PIN_POLARITY_LOW,
                                         .PinPull = PWR_PIN_PULL_DOWN};
    HAL_PWREx_EnableWakeUpPin(&sPinParams);
    // Turn off AMP
    HAL_GPIO_WritePin(AMP_EN_GPIO_Port, AMP_EN_Pin, GPIO_PIN_RESET);

    // Skip codec shutdown if this boot never initialized it.
    if (codecInitialized) {
      tlv320_sleep();
    }
    // TODO: turn off osc

    uint32_t wakeup_time = 300; // wake up every 5 minutes to check
    bool timerArmed =
        (HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, wakeup_time,
                                     RTC_WAKEUPCLOCK_CK_SPRE_16BITS) == HAL_OK);
    // It's very unlikely the above call would fail, but in case it does, we
    // retry applying some remediations
    if (!timerArmed) {
      Trace::Error("PowerOff: failed to arm wakeup timer, retrying once");
      __HAL_RTC_WAKEUPTIMER_CLEAR_FLAG(&hrtc, RTC_FLAG_WUTF);
      HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
      timerArmed =
          (HAL_RTCEx_SetWakeUpTimer_IT(
               &hrtc, wakeup_time, RTC_WAKEUPCLOCK_CK_SPRE_16BITS) == HAL_OK);
      if (!timerArmed) {
        Trace::Error("PowerOff: failed to arm wakeup timer on retry, entering "
                     "standby without periodic wake");
      }
    }

    HAL_PWR_EnterSTANDBYMode();

  } else {
    Trace::Log("POWEROFF", "Deep sleep");
    // BATFET_DIS = 1 as well as: BATFET_RST_EN = 1, TMR2X_EN = 1
    uint8_t value = 0x64;
    HAL_StatusTypeDef status = HAL_I2C_Mem_Write(
        &hi2c4, BQ25601_I2C_ADDR << 1, BQ25601_SHIPMODE_REG,
        I2C_MEMADD_SIZE_8BIT, &value, 1, I2C_TRANSFER_TIMEOUT_MS);
    if (status != HAL_OK) {
      Trace::Error("PowerOff: i2c write error: %i", status);
    }
  }
}

ChargerInt chargerIntReason() {
  // First check if there is any fault active
  // Read FAULT reg
  uint8_t value = 0;
  HAL_StatusTypeDef status = HAL_I2C_Mem_Read(
      &hi2c4, BQ25601_I2C_ADDR << 1, BQ25601_FAULT_REG, I2C_MEMADD_SIZE_8BIT,
      &value, 1, I2C_TRANSFER_TIMEOUT_MS);

  if (status != HAL_OK) {
    Trace::Error("GetCharginStatus: i2c read error: %i", status);
    return UNKNOWN;
  }

  if ((value & CHRG_FAULT_MASK) != CHRG_FAULT_NORMAL) {
    // TODO: we can return different codes if we have different potential
    // actions
    return CHARGE_FAULT;
  }

  // check charge termination and VBUS state
  // Read STATUS reg
  status = HAL_I2C_Mem_Read(&hi2c4, BQ25601_I2C_ADDR << 1, BQ25601_STATUS_REG,
                            I2C_MEMADD_SIZE_8BIT, &value, 1,
                            I2C_TRANSFER_TIMEOUT_MS);

  if (status != HAL_OK) {
    Trace::Error("GetCharginStatus: i2c read error: %i", status);
    return UNKNOWN;
  }

  // check for charge termination
  if ((value & CHRG_STAT_MASK) >> CHRG_STAT_POS == CHRG_STAT_DONE) {
    return CHARGE_END;
  }

  // Check for VBUS OK or KO
  // Read INT reg
  status = HAL_I2C_Mem_Read(&hi2c4, BQ25601_I2C_ADDR << 1, BQ25601_INT_REG,
                            I2C_MEMADD_SIZE_8BIT, &value, 1,
                            I2C_TRANSFER_TIMEOUT_MS);

  if (status != HAL_OK) {
    Trace::Error("GetCharginStatus: i2c read error: %i", status);
    return UNKNOWN;
  }

  if (value & (1 << BIT_VBUS_GD)) {
    return VBUS_OK;
  } else {
    return VBUS_KO;
  }

  return UNKNOWN;
}

bool startCharging(void) {
  uint8_t value;
  HAL_StatusTypeDef status = HAL_I2C_Mem_Read(
      &hi2c4, BQ25601_I2C_ADDR << 1, BQ25601_CHARGING_REG, I2C_MEMADD_SIZE_8BIT,
      &value, 1, I2C_TRANSFER_TIMEOUT_MS);

  if (status != HAL_OK) {
    Trace::Error("GetChargingStatus: i2c read error: %i", status);
    return false;
  }

  value |= (1 << BIT_CHG_CONFIG);
  status = HAL_I2C_Mem_Write(&hi2c4, BQ25601_I2C_ADDR << 1,
                             BQ25601_CHARGING_REG, I2C_MEMADD_SIZE_8BIT, &value,
                             1, I2C_TRANSFER_TIMEOUT_MS);
  if (status != HAL_OK) {
    Trace::Error("i2c write error: %i\r\n", status);
    return false;
  }
  return true;
}

bool stopCharging(void) {
  uint8_t value;
  HAL_StatusTypeDef status = HAL_I2C_Mem_Read(
      &hi2c4, BQ25601_I2C_ADDR << 1, BQ25601_CHARGING_REG, I2C_MEMADD_SIZE_8BIT,
      &value, 1, I2C_TRANSFER_TIMEOUT_MS);

  if (status != HAL_OK) {
    Trace::Error("GetChargingStatus: i2c read error: %i", status);
    return false;
  }

  value &= ~(1 << BIT_CHG_CONFIG);
  status = HAL_I2C_Mem_Write(&hi2c4, BQ25601_I2C_ADDR << 1,
                             BQ25601_CHARGING_REG, I2C_MEMADD_SIZE_8BIT, &value,
                             1, I2C_TRANSFER_TIMEOUT_MS);
  if (status != HAL_OK) {
    Trace::Error("i2c write error: %i\r\n", status);
    return false;
  }
  return true;
}

bool setHostPowerEnabled(bool enabled) {
  uint8_t value = chargerHostPowerReg01Value(enabled);
  const HAL_StatusTypeDef status = HAL_I2C_Mem_Write(
      &hi2c4, BQ25601_I2C_ADDR << 1, BQ25601_CHARGING_REG, I2C_MEMADD_SIZE_8BIT,
      &value, 1, I2C_TRANSFER_TIMEOUT_MS);
  if (status != HAL_OK) {
    Trace::Error("setHostPowerEnabled: i2c write error: %i", status);
    return false;
  }

  Trace::Log("CHARGER", "Host power %s: REG01=0x%02X",
             enabled ? "enabled" : "disabled", value);
  if (!enabled) {
    return true;
  }

  HostPowerPollStatus previous = {};
  bool havePrevious = false;
  for (uint32_t elapsedMs = BQ25601_OTG_POLL_INTERVAL_MS;
       elapsedMs <= BQ25601_OTG_STARTUP_TIMEOUT_MS;
       elapsedMs += BQ25601_OTG_POLL_INTERVAL_MS) {
    hostPowerDelayMs(BQ25601_OTG_POLL_INTERVAL_MS);

    HostPowerPollStatus current = {};
    if (!readHostPowerPollStatus(current)) {
      Trace::Error("CHARGER host power poll failed at %lu ms",
                   static_cast<unsigned long>(elapsedMs));
      return false;
    }

    if (!havePrevious || memcmp(&current, &previous, sizeof(current)) != 0) {
      Trace::Log("CHARGER",
                 "Host power poll t=%lums REG01=0x%02X "
                 "REG08=0x%02X REG09L=0x%02X REG09=0x%02X REG0A=0x%02X",
                 static_cast<unsigned long>(elapsedMs), current.reg01,
                 current.reg08, current.reg09Latched, current.reg09,
                 current.reg0a);
      previous = current;
      havePrevious = true;
    }

    if (chargerVbusStatusIsOtg(current.reg08)) {
      return true;
    }
  }

  uint8_t disabledValue = chargerHostPowerReg01Value(false);
  (void)HAL_I2C_Mem_Write(&hi2c4, BQ25601_I2C_ADDR << 1, BQ25601_CHARGING_REG,
                          I2C_MEMADD_SIZE_8BIT, &disabledValue, 1,
                          I2C_TRANSFER_TIMEOUT_MS);
  logHostPowerDiagnostics("startup-timeout");
  return false;
}

void configureCharger(void) {
  HAL_StatusTypeDef status = HAL_OK;

  // Force the charger into its documented POR register map so boot always
  // starts from a known hardware state, even if the charger stayed powered
  // across MCU resets or firmware updates.
  uint8_t reset_value = (1 << BQ25601_REG_RESET_BIT);
  status = HAL_I2C_Mem_Write(&hi2c4, BQ25601_I2C_ADDR << 1, BQ25601_RESET_REG,
                             I2C_MEMADD_SIZE_8BIT, &reset_value, 1,
                             I2C_TRANSFER_TIMEOUT_MS);
  if (status != HAL_OK) {
    Trace::Error("Charger register reset failed: %i", status);
    return;
  }

  uint8_t watchdog_value = 0;
  status = HAL_I2C_Mem_Read(&hi2c4, BQ25601_I2C_ADDR << 1, BQ25601_TIMER_REG,
                            I2C_MEMADD_SIZE_8BIT, &watchdog_value, 1,
                            I2C_TRANSFER_TIMEOUT_MS);
  if (status != HAL_OK) {
    Trace::Error("Watchdog status read failed: %i", status);
  } else {
    const ChargerWatchdogMode watchdogMode =
        chargerWatchdogModeFromReg05(watchdog_value);
    Trace::Log("CHARGER", "BQ25601 watchdog: %s (REG05=0x%02X)",
               chargerWatchdogModeName(watchdogMode), watchdog_value);

    if (!resetChargerWatchdog()) {
      Trace::Error("Initial charger watchdog refresh failed");
    } else if (!startChargerWatchdogTimer()) {
      Trace::Error("Failed to arm charger watchdog refresh timer");
    }
  }

  uint8_t reg_value = 0;
  status = HAL_I2C_Mem_Read(&hi2c4, BQ25601_I2C_ADDR << 1, BQ25601_STATUS_REG,
                            I2C_MEMADD_SIZE_8BIT, &reg_value, 1,
                            I2C_TRANSFER_TIMEOUT_MS);
  if (status != HAL_OK) {
    Trace::Error("Power status read failed: %i", status);
    return;
  }

  bool powerPresent = (reg_value & BQ25601_PG_STAT) != 0;

  if (powerPresent) {
    if (!startCharging()) {
      Trace::Error("Failed to enable charging during init");
    }
  } else {
    if (!stopCharging()) {
      Trace::Error("Failed to disable charging during init");
    }
  }
}
