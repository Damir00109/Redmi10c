/* camprobe: release reset GPIO + scan CCI i2c bus.
 * usage: camprobe <i2cN> <gpiochipN> <gpioline> [scan|read <addr> <reg16>]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#include <linux/gpio.h>
#include <linux/ioctl.h>
#include <linux/types.h>
#ifndef GPIO_V2_GET_LINE_IOCTL
#define GPIO_V2_LINE_NUM_ATTRS_MAX 10
#define GPIO_V2_LINES_MAX 64
#define GPIO_MAX_NAME_SIZE 32
#define GPIO_V2_LINE_FLAG_OUTPUT (1ULL<<3)
#define GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES 2
struct gpio_v2_line_attribute { __u32 id; __u32 padding; __u64 values; };
struct gpio_v2_line_config_attribute { struct gpio_v2_line_attribute attr; __u64 mask; };
struct gpio_v2_line_config { __u64 flags; __u32 num_attrs; __u32 padding[5]; struct gpio_v2_line_config_attribute attrs[10]; };
struct gpio_v2_line_request { __u32 offsets[64]; char consumer[32]; struct gpio_v2_line_config config; __u32 num_lines; __u32 event_buffer_size; __u32 padding[5]; __s32 fd; };
#define GPIO_V2_GET_LINE_IOCTL _IOWR(0xB4,0x07,struct gpio_v2_line_request)
#endif
#include <stdint.h>

static int gset(int chip, int line, int val){
 char p[64]; snprintf(p,sizeof p,"/dev/gpiochip%d",chip);
 int fd=open(p,O_RDWR); if(fd<0){perror("gpiochip");return -1;}
 struct gpio_v2_line_request req; memset(&req,0,sizeof req);
 req.offsets[0]=line; req.num_lines=1;
 strcpy(req.consumer,"camprobe");
 req.config.flags=GPIO_V2_LINE_FLAG_OUTPUT;
 req.config.attrs[0].attr.id=GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
 req.config.attrs[0].attr.values=val;
 req.config.attrs[0].mask=1;
 req.config.num_attrs=1;
 if(ioctl(fd,GPIO_V2_GET_LINE_IOCTL,&req)<0){perror("get_line");return -1;}
 printf("gpio%d.%d = %d\n",chip,line,val);
 return req.fd;
}
int main(int argc,char**argv){
 int bus=atoi(argv[1]), chip=atoi(argv[2]), line=atoi(argv[3]);
 int gfd=-1;
 if(line>=0){int gval=argc>7?atoi(argv[7]):1; gfd=gset(chip,line,gval);}
 usleep(100000);
 char p[64]; snprintf(p,sizeof p,"/dev/i2c-%d",bus);
 int fd=open(p,O_RDWR); if(fd<0){perror("i2c");return 1;}
 if(argc>4 && !strcmp(argv[4],"read")){
   int addr=strtol(argv[5],0,0), reg=strtol(argv[6],0,0);
   struct i2c_msg m[2]; uint8_t rb[2]={reg>>8,reg&0xff}, db[4]={0};
   struct i2c_rdwr_ioctl_data d;
   m[0].addr=addr;m[0].flags=0;m[0].len=2;m[0].buf=rb;
   m[1].addr=addr;m[1].flags=I2C_M_RD;m[1].len=1;m[1].buf=db;
   d.msgs=m;d.nmsgs=2;
   int r=ioctl(fd,I2C_RDWR,&d);
   printf("read %02x reg %04x: r=%d errno=%d val=%02x\n",addr,reg,r,errno,db[0]);
 } else if(argc>4 && !strcmp(argv[4],"read8")){
   int addr=strtol(argv[5],0,0), reg=strtol(argv[6],0,0);
   struct i2c_msg m[2]; uint8_t rb=reg, db[4]={0};
   struct i2c_rdwr_ioctl_data d;
   m[0].addr=addr;m[0].flags=0;m[0].len=1;m[0].buf=&rb;
   m[1].addr=addr;m[1].flags=I2C_M_RD;m[1].len=1;m[1].buf=db;
   d.msgs=m;d.nmsgs=2;
   int r=ioctl(fd,I2C_RDWR,&d);
   printf("read8 %02x reg %02x: r=%d errno=%d val=%02x\n",addr,reg,r,errno,db[0]);
 } else if(argc>4 && !strcmp(argv[4],"dump8")){
   int addr=strtol(argv[5],0,0);
   if(ioctl(fd,I2C_SLAVE_FORCE,addr)<0){perror("slave");return 1;}
   for(int r=0;r<0x20;r++){
     struct i2c_msg m[2]; uint8_t rb=r, db=0;
     struct i2c_rdwr_ioctl_data d;
     m[0].addr=addr;m[0].flags=0;m[0].len=1;m[0].buf=&rb;
     m[1].addr=addr;m[1].flags=I2C_M_RD;m[1].len=1;m[1].buf=&db;
     d.msgs=m;d.nmsgs=2;
     int ok=ioctl(fd,I2C_RDWR,&d);
     printf("%02x:%s ",r,ok<0?"XX":({static char b[3];snprintf(b,3,"%02x",db);b;}));
     if(r%8==7)printf("\n");}
 } else {
   for(int a=0x08;a<0x78;a++){
     if(ioctl(fd,I2C_SLAVE_FORCE,a)<0)continue;
     uint8_t b; 
     struct i2c_smbus_ioctl_data s; union i2c_smbus_data u;
     s.read_write=I2C_SMBUS_READ;s.command=0;s.size=I2C_SMBUS_BYTE;s.data=&u;
     /* pure address probe: single read msg, no command phase */
     struct i2c_msg m; uint8_t db=0; struct i2c_rdwr_ioctl_data d;
     m.addr=a; m.flags=I2C_M_RD; m.len=1; m.buf=&db; d.msgs=&m; d.nmsgs=1;
     if(ioctl(fd,I2C_RDWR,&d)==1)printf("  ACK 0x%02x (byte=%02x)\n",a,db);
     else if(ioctl(fd,I2C_SMBUS,&s)==0)printf("  found 0x%02x (byte=%02x)\n",a,u.byte&0xff);
   }
 }
 return 0;
}
