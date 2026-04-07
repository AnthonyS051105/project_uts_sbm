#include "flash_storage.h"

#define FLASH_USER_START_ADDR 0x0800C000

uint32_t FlashStorage_Read(uint8_t idx) {
    if (idx > 1) return 0;
    uint32_t val = ((uint32_t *)FLASH_USER_START_ADDR)[idx];
    return (val == 0xFFFFFFFF) ? 0 : val;
}

void FlashStorage_Write(uint8_t idx, uint32_t value) {
    if (idx > 1) return;
    
    uint32_t hs[2] = { FlashStorage_Read(FLASH_HS_IDX_CHARGE), FlashStorage_Read(FLASH_HS_IDX_WHACK) };
    hs[idx] = value;
    
    HAL_FLASH_Unlock();
    
    FLASH_EraseInitTypeDef EraseInitStruct = {
        .TypeErase = FLASH_TYPEERASE_SECTORS,
        .VoltageRange = FLASH_VOLTAGE_RANGE_3,
        .Sector = FLASH_SECTOR_3,
        .NbSectors = 1
    };
    uint32_t SectorError = 0;
    
    if (HAL_FLASHEx_Erase(&EraseInitStruct, &SectorError) == HAL_OK) {
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, FLASH_USER_START_ADDR, hs[0] == 0 ? 0xFFFFFFFF : hs[0]);
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, FLASH_USER_START_ADDR + 4, hs[1] == 0 ? 0xFFFFFFFF : hs[1]);
    }
    
    HAL_FLASH_Lock();
}
