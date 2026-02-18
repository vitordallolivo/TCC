
/*
    Esse codigo usa de base o codigo do ch32v307vtc6 de ethernet
    e cria um socket UDP cliente onde sao mandados por meio de um
    DMA os canais ADC's (PA0 a PA7), todo pos processamento e lidado
    com o python pelo "Server"
 */

#include "string.h"
#include "../NetLib/eth_driver.h"
#include "../NetLib/WCHNET.h"

#define UDP_RECE_BUF_LEN                                    1472    //
#define ADC_DMA_BUFFER_SIZE                                 672     // Tem que se multiplo de 8, so que esse buffer e u16 e nao u8 por isso e metade
#define ADC_CHANNEL_COUNT                                   8
#define ADC_SAMPLE_RATE                                     SystemCoreClock  // Adjust as needed
#define UDP_PACKET_SIZE                                     (8 + 1344) // Header + Data

// PWM Configuration
#define PWM_FREQUENCY                                       8000   // Escolhe um valor
#define PWM_TIMER_PRESCALER                                 (SystemCoreClock / 1000000) - 1  // 1MHz counter
#define PWM_PERIOD                                          (1000000 / PWM_FREQUENCY) - 1    //
#define PWM_DUTY_CYCLE                                      50      // 50% duty cycle

// ADC DMA buffer - aligned for DMA transfer
__attribute__((aligned(4))) u16 ADC_DMA_Buffer[ADC_DMA_BUFFER_SIZE];

// Ethernet buffers
u8 MACAddr[6];                                              // MAC address
u8 IPAddr[4] = {169,254,97,2};                            // IP address
u8 GWIPAddr[4] = {169,254,97,1};                           // Gateway IP address
u8 IPMask[4] = {255, 255, 255, 0};                         // subnet mask

u8 DESIP[4] = {169,254,97,139};                           // destination IP address
u16 desport = 9999;                                         // destination port
u16 srcport = 102;                                         // source port

u8 SocketId;

/*
Isso aqui me deixa meio decepciona com a biblioteca da wchnet, e obrigatorio a existencia de um buffer para receber os dados
*/
u8 SocketRecvBuf[WCHNET_MAX_SOCKET_NUM][UDP_RECE_BUF_LEN]; // socket receive buffer
__attribute__((aligned(4))) u8 ADC_Transmit_Buffer[UDP_PACKET_SIZE];

// ADC DMA control variables
volatile u32 adc_dma_buffer_index = 0;
volatile u8 adc_data_ready = 0;
volatile u32 adc_sample_count = 0;

/*********************************************************************
 * @fn      mStopIfError
 *
 * @brief   check if error.
 *
 * @param   iError - error constants.
 *
 * @return  none
 */
void mStopIfError(u8 iError)
{
    if (iError == WCHNET_ERR_SUCCESS)
        return;
    printf("Error: %02X\r\n", (u16) iError);
}

/*********************************************************************
 * @fn      TIM2_Init
 *
 * @brief   Initializes TIM2.
 *
 * @return  none
 */
void TIM2_Init(void)
{
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure = { 0 };

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    TIM_TimeBaseStructure.TIM_Period = SystemCoreClock / 1000000;
    TIM_TimeBaseStructure.TIM_Prescaler = WCHNETTIMERPERIOD * 1000 - 1;
    TIM_TimeBaseStructure.TIM_ClockDivision = 0;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);
    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);

    TIM_Cmd(TIM2, ENABLE);
    TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
    NVIC_EnableIRQ(TIM2_IRQn);
}

/*********************************************************************
 * @fn      PWM_Init
 *
 * @return  none
 */
void PWM_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    TIM_OCInitTypeDef TIM_OCInitStructure = {0};
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure = {0};
    
    // O TIM8 est¨¢ no barramento APB2 no CH32V307
    // Vide o datasheet (e so olhar)
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC | RCC_APB2Periph_TIM8, ENABLE);
    
    // 2. Configura PC1 como Fun??o Alternativa Push-Pull
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOC, &GPIO_InitStructure);
    
    // Configura a porra do pino com a merda do periodo calculado
    TIM_TimeBaseStructure.TIM_Period = PWM_PERIOD;
    TIM_TimeBaseStructure.TIM_Prescaler = PWM_TIMER_PRESCALER;
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseStructure.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM8, &TIM_TimeBaseStructure);
    

    TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_OutputNState = TIM_OutputNState_Disable; // Sei la o que faz
    TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OCInitStructure.TIM_OCNPolarity = TIM_OCNPolarity_High;
    TIM_OCInitStructure.TIM_OCIdleState = TIM_OCIdleState_Reset;
    TIM_OCInitStructure.TIM_OCNIdleState = TIM_OCNIdleState_Reset;
    TIM_OCInitStructure.TIM_Pulse = (PWM_PERIOD * PWM_DUTY_CYCLE) / 100;
    
    TIM_OC2Init(TIM8, &TIM_OCInitStructure); // Uso de OC2 para Canal 2
    TIM_OC2PreloadConfig(TIM8, TIM_OCPreload_Enable);
    
    // Isso usa o timer avancado do mcu
    TIM_ARRPreloadConfig(TIM8, ENABLE);
    TIM_Cmd(TIM8, ENABLE);
    TIM_CtrlPWMOutputs(TIM8, ENABLE);

}
/*********************************************************************
 * @fn      ADC_DMA_Init
 *
 * @brief   Initializes ADC1 with DMA for channels 0-7.
 *
 * @return  none
 */
void ADC_DMA_Init(void)
{
    ADC_InitTypeDef ADC_InitStructure = {0};
    DMA_InitTypeDef DMA_InitStructure = {0};
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3 | 
                                  GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    

    DMA_DeInit(DMA1_Channel1);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (u32)(&ADC1->RDATAR);
    
    // PONTO CHAVE: O DMA come?a a escrever no ¨ªndice 8 do buffer de transmiss?o
    DMA_InitStructure.DMA_MemoryBaseAddr = (u32)(&ADC_Transmit_Buffer[8]);
    
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;
    DMA_InitStructure.DMA_BufferSize = ADC_DMA_BUFFER_SIZE; // 672 u16
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Circular;
    DMA_InitStructure.DMA_Priority = DMA_Priority_VeryHigh;
    DMA_Init(DMA1_Channel1, &DMA_InitStructure);
    
    DMA_Cmd(DMA1_Channel1, ENABLE);
    
    // Configura??o do ADC
    ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;
    ADC_InitStructure.ADC_ScanConvMode = ENABLE;
    ADC_InitStructure.ADC_ContinuousConvMode = ENABLE;
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_NbrOfChannel = ADC_CHANNEL_COUNT;
    ADC_Init(ADC1, &ADC_InitStructure);
    
    ADC_RegularChannelConfig(ADC1, ADC_Channel_0, 1, ADC_SampleTime_41Cycles5);
    ADC_RegularChannelConfig(ADC1, ADC_Channel_1, 2, ADC_SampleTime_41Cycles5);
    ADC_RegularChannelConfig(ADC1, ADC_Channel_2, 3, ADC_SampleTime_41Cycles5);
    ADC_RegularChannelConfig(ADC1, ADC_Channel_3, 4, ADC_SampleTime_41Cycles5);
    ADC_RegularChannelConfig(ADC1, ADC_Channel_4, 5, ADC_SampleTime_41Cycles5);
    ADC_RegularChannelConfig(ADC1, ADC_Channel_5, 6, ADC_SampleTime_41Cycles5);
    ADC_RegularChannelConfig(ADC1, ADC_Channel_6, 7, ADC_SampleTime_41Cycles5);
    ADC_RegularChannelConfig(ADC1, ADC_Channel_7, 8, ADC_SampleTime_41Cycles5);
    
    ADC_DMACmd(ADC1, ENABLE);
    ADC_Cmd(ADC1, ENABLE);
    ADC_ResetCalibration(ADC1);
    while(ADC_GetResetCalibrationStatus(ADC1));
    ADC_StartCalibration(ADC1);
    while(ADC_GetCalibrationStatus(ADC1));
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
}

/*********************************************************************
 * @fn      ADC_Transmit_Data
 *
 * @brief   Transmit ADC data via UDP
 *
 * @return  none
 */
void ADC_Transmit_Data(void)
{
    u32 len = UDP_PACKET_SIZE;
    u16 samples_in_packet = ADC_DMA_BUFFER_SIZE / ADC_CHANNEL_COUNT;
    
    if (!adc_data_ready) return;
    
    // Atualiza apenas os campos din?micos do cabe?alho (ID da amostra)
    // O marcador 0xAA55 e o tamanho j¨¢ foram definidos no main()
    ADC_Transmit_Buffer[2] = (adc_sample_count >> 24) & 0xFF;
    ADC_Transmit_Buffer[3] = (adc_sample_count >> 16) & 0xFF;
    ADC_Transmit_Buffer[4] = (adc_sample_count >> 8) & 0xFF;
    ADC_Transmit_Buffer[5] = adc_sample_count & 0xFF;

    // Envio direto do buffer que o DMA acabou de preencher
    u8 result = WCHNET_SocketSend(SocketId, ADC_Transmit_Buffer, &len);
    
    if (result == WCHNET_ERR_SUCCESS) {
        adc_sample_count += samples_in_packet;
    }
    
    adc_data_ready = 0;

    DMA_SetCurrDataCounter(DMA1_Channel1, ADC_DMA_BUFFER_SIZE);
    DMA_Cmd(DMA1_Channel1, ENABLE);
}

/*********************************************************************
 * @fn      WCHNET_CreateUdpSocket
 *
 * @brief   Create UDP Socket
 *
 * @return  none
 */
void WCHNET_CreateUdpSocket(void)
{
    u8 i;
    SOCK_INF TmpSocketInf;

    memset((void *) &TmpSocketInf, 0, sizeof(SOCK_INF));
    memcpy((void *) TmpSocketInf.IPAddr, DESIP, 4);
    TmpSocketInf.DesPort = desport;
    TmpSocketInf.SourPort = srcport++;
    TmpSocketInf.ProtoType = PROTO_TYPE_UDP;
    TmpSocketInf.RecvBufLen = UDP_RECE_BUF_LEN;
    i = WCHNET_SocketCreat(&SocketId, &TmpSocketInf);
    printf("SocketId %d\r\n", SocketId);
    WCHNET_ModifyRecvBuf(SocketId, (u32) SocketRecvBuf[SocketId], UDP_RECE_BUF_LEN);
    mStopIfError(i);
}

/*********************************************************************
 * @fn      WCHNET_DataLoopback
 *
 * @brief   Data loopback function. 
 *
 * @param   id - socket id.
 *
 * @return  none
 */
void WCHNET_DataLoopback(u8 id)
{
    u32 len;
    
    len = WCHNET_SocketRecvLen(id, NULL);                // query length
    if (len > 0) {
        WCHNET_SocketRecv(id, ADC_Transmit_Buffer, &len); // read received data
        printf("Received %u bytes via UDP\r\n", len);
        
        // Optional: Echo back or process received data
        // WCHNET_SocketSend(id, ADC_Transmit_Buffer, &len);
    }
}

/*********************************************************************
 * @fn      WCHNET_HandleSockInt
 *
 * @brief   Socket Interrupt Handle
 *
 * @param   socketid - socket id.
 *          intstat - interrupt status
 *
 * @return  none
 */
void WCHNET_HandleSockInt(u8 socketid, u8 intstat)
{
    if (intstat & SINT_STAT_RECV)                             // receive data
    {
        WCHNET_DataLoopback(socketid);                        // Data loopback
    }
    if (intstat & SINT_STAT_CONNECT)                          // connect successfully
    {
        printf("TCP Connect Success\r\n");
    }
    if (intstat & SINT_STAT_DISCONNECT)                       // disconnect
    {
        printf("TCP Disconnect\r\n");
    }
    if (intstat & SINT_STAT_TIM_OUT)                          // timeout disconnect
    {
        printf("TCP Timeout\r\n");
    }
}

/*********************************************************************
 * @fn      WCHNET_HandleGlobalInt
 *
 * @brief   Global Interrupt Handle
 *
 * @return  none
 */
void WCHNET_HandleGlobalInt(void)
{
    u8 intstat;
    u16 i;
    u8 socketint;

    intstat = WCHNET_GetGlobalInt();                          // get global interrupt flag
    if (intstat & GINT_STAT_UNREACH)                          // Unreachable interrupt
    {
        printf("GINT_STAT_UNREACH\r\n");
    }
    if (intstat & GINT_STAT_IP_CONFLI)                        // IP conflict
    {
        printf("GINT_STAT_IP_CONFLI\r\n");
    }
    if (intstat & GINT_STAT_PHY_CHANGE)                       // PHY status change
    {
        i = WCHNET_GetPHYStatus();
        if (i & PHY_Linked_Status)
            printf("PHY Link Success\r\n");
    }
    if (intstat & GINT_STAT_SOCKET) {
        for (i = 0; i < WCHNET_MAX_SOCKET_NUM; i++) {         // socket related interrupt
            socketint = WCHNET_GetSocketInt(i);
            if (socketint)
                WCHNET_HandleSockInt(i, socketint);
        }
    }
}

/*********************************************************************
 * @fn      DMA1_Channel1_IRQHandler
 *
 * @brief   DMA1 Channel1 Interrupt Handler (ADC DMA Transfer Com plete)
 *
 * @return  none
 */
void DMA1_Channel1_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void DMA1_Channel1_IRQHandler(void)
{
    adc_data_ready = 1; 
    DMA_ClearITPendingBit(DMA1_IT_TC1);
}

/*********************************************************************
 * @fn      main
 *
 * @brief   Main program
 *
 * @return  none
 */
int main(void)
{
    u8 i;

    SystemCoreClockUpdate();
    Delay_Init();
    USART_Printf_Init(921600);                                 // USART initialize
    printf("UDP Client with ADC DMA Test\r\n");
    printf("SystemClk:%d\r\n", SystemCoreClock);
    printf("ChipID:%08x\r\n", DBGMCU_GetCHIPID());
    printf("net version:%x\n", WCHNET_GetVer());
    
    if (WCHNET_LIB_VER != WCHNET_GetVer()) {
        printf("version error.\n");
    }
    
    WCHNET_GetMacAddr(MACAddr);                                // get the chip MAC address
    printf("mac addr:");
    for(i = 0; i < 6; i++) 
        printf("%x ", MACAddr[i]);
    printf("\n");
    
    // Initialize peripherals
    TIM2_Init();

    memset(ADC_Transmit_Buffer, 0, UDP_PACKET_SIZE);
    ADC_Transmit_Buffer[0] = 0xAA;
    ADC_Transmit_Buffer[1] = 0x55;
    u16 samples = ADC_DMA_BUFFER_SIZE / ADC_CHANNEL_COUNT;
    ADC_Transmit_Buffer[6] = samples & 0xFF;
    ADC_Transmit_Buffer[7] = (samples >> 8) & 0xFF;

    ADC_DMA_Init();
    
    // Enable DMA interrupt
    DMA_ITConfig(DMA1_Channel1, DMA_IT_TC, ENABLE);
    NVIC_EnableIRQ(DMA1_Channel1_IRQn);
    
    // Ethernet library initialize
    i = ETH_LibInit(IPAddr, GWIPAddr, IPMask, MACAddr);
    mStopIfError(i);
    
    if (i == WCHNET_ERR_SUCCESS)
        printf("WCHNET_LibInit Success\r\n");
    
    // Create UDP Socket
    WCHNET_CreateUdpSocket();
    
    // Main loop
    while(1)
    {
        /* Ethernet library main task function,
         * which needs to be called cyclically */
        WCHNET_MainTask();
        
        /* Query the Ethernet global interrupt,
         * if there is an interrupt, call the global interrupt handler */
        if(WCHNET_QueryGlobalInt())
        {
            WCHNET_HandleGlobalInt();
        }
        
        /* Check if ADC data is ready and transmit via UDP */
        if (adc_data_ready==1) {
            ADC_Transmit_Data();
        }
        
    }
}