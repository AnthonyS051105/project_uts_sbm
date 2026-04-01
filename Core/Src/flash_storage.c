#include "flash_storage.h"

// Sector 3 (0x0800C000) for highscores, size 16KB
#define FLASH_USER_START_ADDR   0x0800C000

uint32_t FlashStorage_Read(uint8_t idx) {
    uint32_t *flash_addr = (uint32_t *)FLASH_USER_START_ADDR;
    if (idx > 1) return 0;
    
    uint32_t val = flash_addr[idx];
    if (val == 0xFFFFFFFF) val = 0;
    return val;
}

void FlashStorage_Write(uint8_t idx, uint32_t value) {
    if (idx > 1) return;
    
    uint32_t current_highscore[2];
    current_highscore[0] = FlashStorage_Read(FLASH_HS_IDX_CHARGE);
    current_highscore[1] = FlashStorage_Read(FLASH_HS_IDX_WHACK);
    
    current_highscore[idx] = value;
    
    HAL_FLASH_Unlock();
    
    FLASH_EraseInitTypeDef EraseInitStruct;
    uint32_t SectorError = 0;
    EraseInitStruct.TypeErase = FLASH_TYPEERASE_SECTORS;
    EraseInitStruct.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    EraseInitStruct.Sector = FLASH_SECTOR_3;
    EraseInitStruct.NbSectors = 1;
    
    if (HAL_FLASHEx_Erase(&EraseInitStruct, &SectorError) == HAL_OK) {
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, FLASH_USER_START_ADDR, current_highscore[0] == 0 ? 0xFFFFFFFF : current_highscore[0]);
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, FLASH_USER_START_ADDR + 4, current_highscore[1] == 0 ? 0xFFFFFFFF : current_highscore[1]);
    }
    
    HAL_FLASH_Lock();
}
