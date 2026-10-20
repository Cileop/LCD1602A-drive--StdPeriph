/* ============================================================================
 *  verify/fake/stm32f10x.h
 *
 *  Host-side shadow of the STM32F10x device header.  It is ONLY used for the
 *  offline simulation build (g++ -I verify/fake ...).  The real firmware
 *  sources are compiled completely unchanged; this header provides the
 *  peripheral objects as C++ proxy objects whose operator= / operator|=
 *  / operator& record every access into the simulation timeline.
 *
 *  It also declares a small, faithful subset of the StdPeriph API so that a
 *  rewritten driver which uses GPIO_Init()/RCC_APB2PeriphClockCmd() still
 *  compiles (those functions are implemented as no-ops that log).
 * ========================================================================== */
#ifndef DSH_VERIFY_FAKE_STM32F10X_H
#define DSH_VERIFY_FAKE_STM32F10X_H

#include <stdint.h>
#include <stddef.h>

/* ---------------------------------------------------------------- enums --- */
typedef enum { DISABLE = 0, ENABLE = 1 } FunctionalState;
typedef enum { RESET = 0, SET = 1 } FlagStatus, ITStatus;
typedef enum { Bit_RESET = 0, Bit_SET = 1 } BitAction;

typedef enum
{
    GPIO_Speed_10MHz = 1,
    GPIO_Speed_2MHz  = 2,
    GPIO_Speed_50MHz = 3
} GPIOSpeed_TypeDef;

typedef enum
{
    GPIO_Mode_AIN         = 0x00,
    GPIO_Mode_IN_FLOATING = 0x04,
    GPIO_Mode_IPD         = 0x28,
    GPIO_Mode_IPU         = 0x48,
    GPIO_Mode_Out_OD      = 0x14,
    GPIO_Mode_Out_PP      = 0x10,
    GPIO_Mode_AF_OD       = 0x1C,
    GPIO_Mode_AF_PP       = 0x18
} GPIOMode_TypeDef;

typedef struct
{
    uint16_t       GPIO_Pin;
    GPIOSpeed_TypeDef GPIO_Speed;
    GPIOMode_TypeDef  GPIO_Mode;
} GPIO_InitTypeDef;

#define GPIO_Pin_0   ((uint16_t)0x0001)
#define GPIO_Pin_1   ((uint16_t)0x0002)
#define GPIO_Pin_2   ((uint16_t)0x0004)
#define GPIO_Pin_3   ((uint16_t)0x0008)
#define GPIO_Pin_4   ((uint16_t)0x0010)
#define GPIO_Pin_5   ((uint16_t)0x0020)
#define GPIO_Pin_6   ((uint16_t)0x0040)
#define GPIO_Pin_7   ((uint16_t)0x0080)
#define GPIO_Pin_8   ((uint16_t)0x0100)
#define GPIO_Pin_9   ((uint16_t)0x0200)
#define GPIO_Pin_10  ((uint16_t)0x0400)
#define GPIO_Pin_11  ((uint16_t)0x0800)
#define GPIO_Pin_12  ((uint16_t)0x1000)
#define GPIO_Pin_13  ((uint16_t)0x2000)
#define GPIO_Pin_14  ((uint16_t)0x4000)
#define GPIO_Pin_15  ((uint16_t)0x8000)
#define GPIO_Pin_All ((uint16_t)0xFFFF)

#define RCC_APB2Periph_AFIO   ((uint32_t)0x00000001)
#define RCC_APB2Periph_GPIOA  ((uint32_t)0x00000004)
#define RCC_APB2Periph_GPIOB  ((uint32_t)0x00000008)
#define RCC_APB2Periph_GPIOC  ((uint32_t)0x00000010)
#define RCC_APB1Periph_TIM2   ((uint32_t)0x00000001)
#define RCC_APB1Periph_I2C1   ((uint32_t)0x00200000)

#ifndef assert_param
#define assert_param(expr) ((void)0)
#endif
#define __IO volatile

/* ------------------------------------------------------------ proxy reg --- */
/* Every GPIO/RCC/SysTick register is one of these.  The implementation lives
 * in verify/src/harness.cpp and records accesses into the timeline. */
class RegProxy
{
public:
    enum Kind
    {
        K_GPIO_CRL = 1, K_GPIO_CRH, K_GPIO_IDR, K_GPIO_ODR,
        K_GPIO_BSRR, K_GPIO_BRR,
        K_RCC_APB2ENR, K_RCC_APB1ENR, K_RCC_AHBENR,
        K_SYST_CTRL, K_SYST_LOAD, K_SYST_VAL, K_SYST_CALIB
    };

    RegProxy();
    void bind(int kind, int port);

    /* writes */
    uint32_t operator=(uint32_t v);
    uint32_t operator|=(uint32_t v);
    uint32_t operator&=(uint32_t v);

    /* reads (used as  SysTick->CTRL & mask  and  GPIOB->IDR & mask) */
    uint32_t operator&(uint32_t mask) const;
    uint32_t operator&(int mask) const;      /* keeps int literals unambiguous */
    operator uint32_t() const;

    uint32_t value() const;
    int kind() const { return kind_; }
    int port() const { return port_; }

private:
    int kind_;
    int port_;
};

/* ----------------------------------------------------------- peripherals --- */
struct GPIO_TypeDef
{
    RegProxy CRL, CRH, IDR, ODR, BSRR, BRR, LCKR;
    GPIO_TypeDef();
};

struct RCC_TypeDef
{
    RegProxy CR, CFGR, CIR, APB2RSTR, APB1RSTR, AHBENR, APB2ENR, APB1ENR, BDCR, CSR;
    RCC_TypeDef();
};

struct SysTick_TypeDef
{
    RegProxy CTRL, LOAD, VAL, CALIB;
    SysTick_TypeDef();
};

extern GPIO_TypeDef g_GPIOA;
extern GPIO_TypeDef g_GPIOB;
extern GPIO_TypeDef g_GPIOC;
extern RCC_TypeDef  g_RCC;
extern SysTick_TypeDef g_SysTick;

#define GPIOA   (&g_GPIOA)
#define GPIOB   (&g_GPIOB)
#define GPIOC   (&g_GPIOC)
#define RCC     (&g_RCC)
#define SysTick (&g_SysTick)

/* ----------------------------------------------- StdPeriph function stubs --- */
void RCC_APB2PeriphClockCmd(uint32_t periph, FunctionalState state);
void RCC_APB1PeriphClockCmd(uint32_t periph, FunctionalState state);
void RCC_AHBPeriphClockCmd(uint32_t periph, FunctionalState state);

void GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *init);
void GPIO_SetBits(GPIO_TypeDef *port, uint16_t pins);
void GPIO_ResetBits(GPIO_TypeDef *port, uint16_t pins);
void GPIO_WriteBit(GPIO_TypeDef *port, uint16_t pin, BitAction value);
uint8_t  GPIO_ReadInputDataBit(GPIO_TypeDef *port, uint16_t pin);
uint16_t GPIO_ReadInputData(GPIO_TypeDef *port);
uint16_t GPIO_ReadOutputData(GPIO_TypeDef *port);
void GPIO_Write(GPIO_TypeDef *port, uint16_t value);

#endif /* DSH_VERIFY_FAKE_STM32F10X_H */
