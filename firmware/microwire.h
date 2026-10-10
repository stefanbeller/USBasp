//Functions for sw microwire interface
void mwBegin(void);

void mwStart(void);

void mwSendData(uint16_t data, uint8_t bits);

uint8_t mwReadByte();

void mwEnd();

uint8_t mwBusy();

void mwReadDummyBit(void);