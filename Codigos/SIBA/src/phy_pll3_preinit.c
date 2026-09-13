/*
 * Configura PLL3 + EXTEN_ETH_10M_EN em POST_KERNEL+55, antes de phy_mii_init
 * (POST_KERNEL+60). Sem PLL3 ativo, as linhas MDIO da placa flutuam para
 * 0xFFFF, o reset do PHY nunca limpa (-ETIMEDOUT), phy_mii_init retorna antes
 * de montar o monitor_work, e o link nunca sobe.
 *
 * Valores idênticos ao que eth_wch_init aplicaria depois — DTS siba_board.dts:
 *   internal-phy-pllmul = "15"  →  enum idx 13
 *   internal-phy-prediv = "2"   →  enum idx  1
 */
#ifndef CONFIG_ARCH_POSIX
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <hal_ch32fun.h>

#define PHY_PLLMUL_IDX  13U
#define PHY_PREDIV_IDX   1U
#define WCH_PLL3MUL(m)  (((uint32_t)(m) << 0x0CU) & CFGR2_PLL3MUL)
#define WCH_PREDIV2(d)  (((uint32_t)(d) << 0x04U) & CFGR2_PREDIV2)

static int eth_phy_pll3_preinit(void)
{
    RCC->CTLR  &= ~RCC_PLL3ON;
    RCC->CFGR2 &= ~CFGR2_PREDIV2;
    RCC->CFGR2 |=  WCH_PREDIV2(PHY_PREDIV_IDX);
    RCC->CFGR2 &= ~CFGR2_PLL3MUL;
    RCC->CFGR2 |=  WCH_PLL3MUL(PHY_PLLMUL_IDX);
    RCC->CTLR  |=  RCC_PLL3ON;
    while (!(RCC->CTLR & RCC_PLL3RDY)) {}
    EXTEN->EXTEN_CTR |= EXTEN_ETH_10M_EN;
    return 0;
}
SYS_INIT(eth_phy_pll3_preinit, POST_KERNEL, 55);
#endif
