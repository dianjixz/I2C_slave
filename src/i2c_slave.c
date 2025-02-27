

#include <stdint.h>
#include <stdlib.h>
#include "i2c_slave.h"



#define I2C_SCL_IN        
#define I2C_SCL_OUT       
#define I2C_SDA_IN        
#define I2C_SDA_OUT       


#define I2C_SCL_IO        
#define I2C_SDA_IO        

#define I2C_SCL_HIGH      
#define I2C_SCL_LOW       
#define I2C_SDA_HIGH      
#define I2C_SDA_LOW       
#define INTTERUPT_OFF     
#define INTTERUPT_ON      


struct MY_I2C_SLAVE my_i2c_slave;



extern int i2c_flages;

static int i2c_wait_scl(int val);
static uint8_t i2c_read_byte();
static void i2c_sda_set(int32_t val);
static void i2c_write_byte(uint8_t val);
static void i2c_slave_send_ack();
static void i2c_slave_get_ack();
static void i2c_pins_init(void);
static void i2c_slave_interrupt(void);


void my_i2c_slave_init(void)
{
  //gpio_init;
  struct MY_I2C_SLAVE *myslave = &my_i2c_slave;
  myslave->i2c_flage = 0;
  myslave->data_offs = 0;
  i2c_pins_init();
  while (I2C_SCL_IO == IO_LOW || I2C_SDA_IO == IO_LOW);
}

void i2c_event_selet(void)
{
  INTTERUPT_OFF;
  if(I2C_SCL_IO == IO_HIGH && I2C_SDA_IO == IO_LOW)
  {
    my_i2c_slave.i2c_flage |= I2C_STATE_USE;
    i2c_slave_interrupt();
  }
  INTTERUPT_ON;
}


static void i2c_slave_interrupt(void)
{
  struct MY_I2C_SLAVE *myslave = &my_i2c_slave;
  uint8_t data = 0;
  for (;;)
  {
    switch (myslave->i2c_flage)
    {
    case 0x80:    //i2c总线开始活动,读取i2c总线上的地址
      i2c_wait_scl(IO_LOW);
      data = i2c_read_byte();
      if((data>>1) == I2C_SLAVE_ADDRESS)          //判断地址是否符合
      {
        i2c_slave_send_ack();                     //地址符合后发送应答
        myslave->i2c_flage |= I2C_STATE_DEVICE;   //置总线设备标志位
        if(data & 0x01)                           //判断总些读写,并置读写位
        {
            myslave->i2c_flage |= I2C_STATE_WR;
        }
      }
      else          //地址如果不符,进入下一阶段
      {
        myslave->i2c_flage ++;
      }
      break;
    case 0x81:    //地址不符,持续读取总线上的数据
      i2c_read_byte();
      break;
    case 0xC0:    //主机写  接受数据
      data = i2c_read_byte();
      myslave->i2c_flage ++;
      break;
    case 0xC1:    //主机写 发送应答并存储数据
      i2c_slave_send_ack();
      myslave->i2c_flage --;
      myslave->data[myslave->data_offs] = data;
      myslave->data_offs ++;
      i2c_flages = 1;
      break;
    case 0xE0:    //主机读
      data = buf_pop();
      i2c_write_byte(data);
      myslave->i2c_flage ++;
      break;
    case 0xE1:    //主机读
      i2c_slave_get_ack();
      myslave->i2c_flage ++;
      break;

    case 0xE2 :   //无应答
      i2c_wait_scl(1);
      uint32_t times = I2C_SLAVE_TIMEOUT;
      while (times --)
      {
        if(I2C_SDA_IO == IO_HIGH)
        {
          goto end;
        }
      }
      goto end;
      break;

    case 0xF2:    //有应答
      data = buf_pop();
      i2c_write_byte(data);
      myslave->i2c_flage ++;
      break;
    case 0xF3:
      i2c_slave_get_ack();
      myslave->i2c_flage --;
      break;

    default:
      i2c_wait_scl(1);
      uint32_t timesc = I2C_SLAVE_TIMEOUT;
      while (timesc --)
      {
        if(I2C_SDA_IO == IO_HIGH)
        {
          goto end;
        }
      }
      goto end;
      break;
    }
  }
  end:
  i2c_pins_init();
  myslave->i2c_flage = 0;
}


static void i2c_pins_init(void)
{
  I2C_SDA_IN;
  I2C_SCL_IN;
}


static int i2c_wait_scl(int val)
{
  struct MY_I2C_SLAVE *myslave = &my_i2c_slave;
  uint32_t i = I2C_SLAVE_TIMEOUT;
  while (i --)
  {
    if(I2C_SCL_IO == val) return 0;
  }
  myslave->i2c_flage |= I2C_STATE_TIMEOUT;
  return -1;
}


static uint8_t i2c_read_byte()
{
  struct MY_I2C_SLAVE *myslave = &my_i2c_slave;
  uint8_t val;
  int count;
  int32_t temp;

  for (int i = 0; i < 8; i++)
  {
    //TODO:timeout check
    if(i2c_wait_scl(1) < 0) return 0;

    val = (val << 1) | I2C_SDA_IO;

    count = I2C_SLAVE_TIMEOUT;
    while (I2C_SCL_IO)
    { //等待高点平结束，判断是否出现异常情况
      //TODO:timeout check
      /* sda is drivered by master now.
       * if it changes when scl is high,stop or start happened */
      if ((count--) == 0)
      {
        myslave->i2c_flage |= I2C_STATE_TIMEOUT;
        return 0;
      }
      temp = I2C_SDA_IO; //读取数据线
      if (!I2C_SCL_IO) break;   //当时钟线为低电平时退出循环
      if ((val & 0x01) != (temp & 0x01)) //判断数据线是否发生了跳变
      {
        if (temp) //数据线被释放
        {
          myslave->i2c_flage &= ~I2C_STATE_USE;           //低到高跳变,总线被释放
        }
        else
        {
          myslave->i2c_flage = I2C_STATE_USE;     //高到低跳变改变总线状态为读取地址状态
          i2c_wait_scl(0);
        }
        return 0;
      }
    }
  }
  return val;
}


static void i2c_sda_set(int32_t val)
{
  if (val)
  {
    I2C_SDA_IN;
  }
  else
  {
    I2C_SDA_LOW;
    I2C_SDA_OUT;
  }
}


static void i2c_write_byte(uint8_t val)
{
  // uint8_t data = 0x80;
  for (int i = 0; i < 8; i++)
  {
    i2c_sda_set(val & 0x80);
    val = val << 1;
    if (i2c_wait_scl(1) < 0) return ;
    if (i2c_wait_scl(0) < 0) return ;
  }
  i2c_sda_set(1);
}


static void i2c_slave_send_ack()
{

  /* slave driver sda to low for ACK */
  i2c_sda_set(0);
  /* wait master read(scl rising edge trigger) ACK */
  /* wait scl to HIGH */
  if (i2c_wait_scl(1) < 0) return ;
  /* wait scl to low */
  if (i2c_wait_scl(0) < 0) return ;

  i2c_sda_set(1);
}

static void i2c_slave_get_ack()
{
  struct MY_I2C_SLAVE *myslave = &my_i2c_slave;
  if (i2c_wait_scl(1) < 0) return ;
  if(I2C_SDA_IO)
  {
    myslave->i2c_flage &= ~I2C_STATE_ACK;
  }
  else
  {
    myslave->i2c_flage |= I2C_STATE_ACK;
  }
  i2c_wait_scl(0);
}



