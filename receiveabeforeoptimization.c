#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <linux/types.h>
#include <inttypes.h>
#include <string.h>
#include <mysql/mysql.h>
#include <sys/time.h>
#include <pthread.h>
#include "thread1.h"
#include "hash_lin.h"
#include "cbuf.h"

#define buffer_size_class1 10000
#define buffer_size_public 10000
int written_to_mysql = 0;
pthread_mutex_t mutex, mutex_public_buff; 
pthread_cond_t cond, cond_public_buff;

int packet6682 = 0, packet6681 = 0, packet6683 = 0, packet6666 = 0;
int written_to_mysql_iw = 0, written_to_mysql_beacon = 0, written_to_mysql_queue = 0, written_to_mysql_winsize = 0;
char *ringbuff6666[buffer_size_class1];//four buffer for four threads.
char *ringbuff6682[buffer_size_class1];
char *ringbuff6681[buffer_size_class1];
char *ringbuff6683[buffer_size_class1];
char *publicbuff[buffer_size_public];
int write_start6666 = 0, write_end6666 = 0;
int write_start6682 = 0, write_end6682 = 0;
int write_start6681 = 0, write_end6681 = 0;
int write_start6683 = 0, write_end6683 = 0;
int write_start_public = 0, write_end_public = 0;
int public_buff_size = 0;
static __u64 getcurrenttime(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
    // return 1;
}

void reversebytes_uint16t(__u16 *value)
{
    *value = (*value & 0x00FF) << 8 |(*value & 0xFF00) >> 8;
}
void reversebytes_uint32t(__u32 *value)
{
  *value = (*value & 0x000000FFU) << 24 | (*value & 0x0000FF00U) << 8 | 
    (*value & 0x00FF0000U) >> 8 | (*value & 0xFF000000U) >> 24;
}

void reversebytes_uint64t(__u64 *value)
{
  __u64 low = (*value & 0x00000000FFFFFFFF);
  __u64 high = (*value & 0xFFFFFFFF00000000) >> 32;
  __u32 low_32 = (__u32) low;
  __u32 high_32 = (__u32) high;
  reversebytes_uint32t(&low_32);
  reversebytes_uint32t(&high_32);
  low = ((__u64) low_32) << 32;
  high = ((__u64) high_32);
  *value = low | high;
}

void inttou64(char tmp[], __u64 n)
{
  memcpy(tmp, &n, 8);
}

__u64 chartou64(char a[])
{
  __u64 n = 0;
  memcpy(&n, a, 8);
  return n;
}

unsigned long strtou32(char *str) 
{
    unsigned long temp=0;
    unsigned long fact=1;
    unsigned char len=strlen(str);
    unsigned char i;
    for(i=len;i>0;i--)
    {
        temp+=((str[i-1]-0x30)*fact);
        fact*=10;
    }
    return temp;

}

void u16tostr(__u16 dat,char *str) 
{
    char temp[20];
    unsigned char i=0,j=0;
    i=0;
    while(dat)
    {
        temp[i]=dat%10+0x30;
        i++;
        dat/=10;
    }
    j=i;
    for(i=0;i<j;i++)
    {
      str[i]=temp[j-i-1];
    }
    if(!i) 
    {
        str[i++]='0';
    }
    str[i]=0;
}

void mac_tranADDR_toString_r(unsigned char* addr, char* str, size_t size)
{
    if(addr == NULL || str == NULL || size < 18)
      exit(1);

    snprintf(str, size, "%02x:%02x:%02x:%02x:%02x:%02x", 
             addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
    str[17] = '\0';
}

static void agg_buff(char **buffer, int *write_begin, int write_end)
{
  pthread_mutex_lock(&mutex_public_buff);
  while((write_end -1) > *write_begin)
  {
    // printf("buffer[*write_begin] is %s *write_begin is %d, write_end is %d\n", buffer[*write_begin], *write_begin, write_end);
    memcpy(publicbuff[write_end_public], buffer[*write_begin], strlen(buffer[*write_begin]));
    write_end_public++;
    write_end_public = write_end_public % buffer_size_public;
    public_buff_size++;
    // printf("public_buff_size is %d \n", public_buff_size);
    // buffer[*write_begin][2] = '\0';
    *write_begin = *write_begin + 1;
    *write_begin = *write_begin % buffer_size_class1;
  }
  if (public_buff_size > 1000)
  {
    pthread_cond_signal(&cond_public_buff);
    // printf("here\n");
  }
  pthread_mutex_unlock(&mutex_public_buff);
}

void write_to_public_buffer(char **buffer, int *write_start, int write_end)
{
  int data_amount = 0;
  if (!((written_to_mysql_beacon + written_to_mysql_queue + written_to_mysql_iw + written_to_mysql_winsize) % 500))
  {
    printf("all the packets is %d 6666 %d 6682 %d 6681 %d 6683 %d public_buff_size %d, \n",packet6682 + packet6681 + packet6683 + packet6666, packet6666, packet6682, packet6681, packet6683,public_buff_size);
    printf(" written_to_mysql %d all %d beacon %d iw %d queue %d winsize %d\n", written_to_mysql, (written_to_mysql_beacon + written_to_mysql_queue + written_to_mysql_iw + written_to_mysql_winsize), written_to_mysql_beacon, written_to_mysql_iw, written_to_mysql_queue, written_to_mysql_winsize);
  }
  if (write_end > *write_start)
    data_amount = write_end - *write_start;
  else
    data_amount = write_end + buffer_size_class1 - *write_start;
  if (data_amount > 1000) //start a thread to write to public buffer
  {
    agg_buff(buffer, write_start, write_end);
    // pthread_t tid1;
    // int err = 0, i = 0;
    // err = pthread_create(&tid1, NULL, agg_buff(buffer, write_start, write_end), NULL);
    // if(err != 0)
    // {
    //   printf("agg_buff%d creation failed \n", i);
    //   // exit(1);
    // }
    // pthread_exit(NULL);
  }
}
static void *receive6682(void *arg)
{
  const int Port6682 = 6682;
  const int Win6682 = 2048;
  const int Reclen6682 = 88;
  struct data_beacon
  {
      unsigned char mac_addr[8];
      unsigned char mac_timestamp[8];
      unsigned char data_rate[4];
      unsigned char current_channel[4];
      unsigned char channel_type[4];
      unsigned char ssl_signal[4];
      __u64 time_current;
      unsigned char bssid[8]; // character array for BSSID and ESSID
      unsigned char essid[40];
  };
  int sockfd; //socket
  int len; //used to get the length of socket
  struct sockaddr_in addr;
  int addr_len = sizeof(struct sockaddr_in); //socket
  char buffer[Win6682];  //1024 buffer of readbuffer
  struct data_beacon beacon;
  int read_end = 0; //buffer length
  int offset = 0; //offset of read
  __u64 mac_timestamp, time; //
  signed char data_rate;
  int current_channel, ssl_signal;
  char channel_type[] = "802.11a";
  char mac_addr_beacon[18], bssid[18];
  

  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
      perror ("socket");
      exit(1);
  }
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(Port6682);
  addr.sin_addr.s_addr = htonl(INADDR_ANY) ;// 接收任意IP发来的数据

  if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr))<0) {
      perror("connect");
      exit(1);
  }

  while(1) 
    { 
      
      char insert_data[300];
      int i;
      memset(&beacon, 0, sizeof(beacon));
      memset(buffer, 0, sizeof(buffer));
      len = recvfrom(sockfd, buffer, sizeof(buffer), 0, 
                     (struct sockaddr *)&addr ,&addr_len);
      read_end = Win6682;
      i = 0;
      if (len != -1)
        packet6682++;
      offset = 0;
      while((read_end > (offset + Reclen6682)))
      {
        int i = 0;
        int key = 0;
        memcpy(&beacon, buffer+offset, Reclen6682);
        if(beacon.time_current !=0)
        {
          offset = offset + Reclen6682;
          reversebytes_uint64t(&beacon.time_current);
          time = beacon.time_current;
          mac_timestamp = 0;
          memcpy(&mac_timestamp, beacon.mac_timestamp, 8);;
          ssl_signal = (signed char)beacon.ssl_signal[0];
          current_channel = (int)beacon.current_channel[1] * 256 + (int)beacon.current_channel[0];
          data_rate = (int)beacon.data_rate[0];
          if (key == 0)
          {
            memset(mac_addr_beacon, 0, strlen(mac_addr_beacon));
            mac_tranADDR_toString_r(beacon.mac_addr, mac_addr_beacon, 18);
            key = 1;
          }
          mac_tranADDR_toString_r(beacon.bssid, bssid, 18);

          if ((int)beacon.channel_type[0] == 160 && (int)beacon.channel_type[1] == 0)
          {
            strcpy(channel_type, "802.11b");
          }
          else if ((int)beacon.channel_type[0] == 192 && (int)beacon.channel_type[1] == 0)
          {
            strcpy(channel_type, "802.11g");
          }

          memset(insert_data, 0, 300);
          sprintf(insert_data, "INSERT INTO Beacon(time, mac_timestamp, data_rate, current_channel, channel_type, ssl_signal, bw, bssid, essid, mac_addr) VALUES(%llu, %llu, %d, %d, \"%s\", %d, %d, \"%.18s\", \"%s\", \"%.18s\")", time, mac_timestamp, data_rate, current_channel, channel_type, ssl_signal, 0,  bssid, beacon.essid, mac_addr_beacon);
          insert_data[strlen(insert_data)] = '\0';
          written_to_mysql_beacon ++;
          strncpy(ringbuff6682[write_end6682], insert_data, strlen(insert_data));
          write_end6682++;
          write_end6682 = write_end6682 % buffer_size_class1;
          write_to_public_buffer(ringbuff6682, &write_start6682, write_end6682);
        }
        else
          offset = 2* Win6682;
      }
      usleep(100000);
    }

}
static void *receie6681(void *arg)
{
  const int Port6681 = 6681;
  const int Win6681 = 2048;
  const int Reclen6681 = 80;

  struct data_queue {
    __u64 time;
    __u32 queue_id;
    __u32 pad;
    __u64 bytes;
    __u32 packets;
    __u32 qlen;
    __u32 backlog;
    __u32 drops;
    __u32 requeues;
    __u32 overlimits;
    __s32 deficit;
    __u32 ldelay; 
    __u32 count;
    __u32 lastcount;
    __u32 dropping;
    __s32 drop_next;
    char mac_addr[8];
  };

    struct sockaddr_in addr;
    int sockfd, len = 0;    
    int addr_len = sizeof(struct sockaddr_in);
    char buffer[Win6681];  
    struct data_queue rdata;
    int read_end = 0;
    int offset = 0;
    char mac_addr[18];
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror ("socket");
        exit(1);
    }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(Port6681);
    addr.sin_addr.s_addr = htonl(INADDR_ANY) ;

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr))<0) {
        perror("connect");
        exit(1);
    }

    while(1) 
      { 
        char insert_data[500];
        
        memset(buffer, 0, sizeof(buffer));
        len = recvfrom(sockfd, buffer, sizeof(buffer), 0, 
                       (struct sockaddr *)&addr ,&addr_len);
        read_end = Win6681;
        if (len != -1)
          packet6681++;
        offset = 0;
        while((read_end > (offset + Reclen6681)))
        {
          int key = 0;
          memcpy(&rdata, buffer+offset, Reclen6681);
          if(rdata.time !=0)
          {
            offset = offset + Reclen6681;
            reversebytes_uint64t(&rdata.bytes);
            reversebytes_uint64t(&rdata.time);        
            reversebytes_uint32t(&rdata.queue_id);
            reversebytes_uint32t(&rdata.packets);
            reversebytes_uint32t(&rdata.qlen);
            reversebytes_uint32t(&rdata.backlog);
            reversebytes_uint32t(&rdata.drops);
            reversebytes_uint32t(&rdata.requeues);
            reversebytes_uint32t(&rdata.overlimits);
            reversebytes_uint32t(&rdata.deficit);
            reversebytes_uint32t(&rdata.ldelay);
            reversebytes_uint32t(&rdata.count);
            reversebytes_uint32t(&rdata.lastcount);
            reversebytes_uint32t(&rdata.dropping);
            reversebytes_uint32t(&rdata.drop_next);
            memset(insert_data, 0, 500);
            sprintf(insert_data, "INSERT INTO queue(mac_ddr, time, queue_id, bytes, packets, qlen, backlog, drops, requeues, overlimits, deficit, ldelay, count, lastcount, dropping, drop_next) VALUES(\"%.18s\", %llu, %u, %llu, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d ,%d ,%d)", mac_addr, rdata.time, rdata.queue_id, rdata.bytes, rdata.packets, rdata.qlen, rdata.backlog, rdata.drops, rdata.requeues, rdata.overlimits, rdata.deficit, rdata.ldelay, rdata.count, rdata.lastcount, rdata.dropping, rdata.drop_next);
            insert_data[strlen(insert_data)] = '\0';
            written_to_mysql_queue++;
            if (key == 0)
            {
              memset(mac_addr, 0, sizeof(mac_addr));
              mac_tranADDR_toString_r(rdata.mac_addr, mac_addr, 18);
              key = 1;
            }
            strncpy(ringbuff6681[write_end6681], insert_data, strlen(insert_data));
            write_end6681++;
            write_end6681 = write_end6681 % buffer_size_class1;
            write_to_public_buffer(ringbuff6681, &write_start6681, write_end6681);

          }
          else
            offset = 2* Win6681;
        }
        usleep(100000);
      }

}
static void *receive6683(void *arg)
{
  const int Port6683 = 6683;
  const int Win6683 = 2048;
  const int Reclen6683 = 136;

  struct data_iw
  {
      __u64 time;
      char mac_addr[8];
      char device[8];
      __u32 inactive_time;
      __u32 rx_bytes;
      __u32 rx_packets;
      __u32 tx_bytes;
      __u32 tx_packets;
      __u32 tx_retries;
      __u32 tx_failed;
      __s32 signal;
      __s32 signal_avg;
      __u32 pad;
      char tx_bitrate[32];
      char rx_bitrate[32];
      __u32 expected_throughput;
      __u32 connected_time;

  };

  struct sockaddr_in addr;
  int sockfd, len = 0;    
  int addr_len = sizeof(struct sockaddr_in);
  char buffer[Win6683];  
  struct data_iw rdata;
  int read_end = 0;
  int offset = 0;

  __u32 expected_throughput_tmp;
  float expected_throughput;


  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
      perror ("socket");
      exit(1);
  }
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(Port6683);
  addr.sin_addr.s_addr = htonl(INADDR_ANY) ;

  if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr))<0) {
      perror("connect");
      exit(1);
  }

  while(1) 
    { 
      char insert_data[500];
      char mac_addr[18];

      memset(buffer, 0, sizeof(buffer));
      len = recvfrom(sockfd, buffer, sizeof(buffer), 0, 
                     (struct sockaddr *)&addr ,&addr_len);
      read_end = Win6683;

      offset = 0;
      if (len != -1)
        packet6683++;
      while((read_end > (offset + Reclen6683)))
      {
        int key = 0;
        memcpy(&rdata, buffer+offset, Reclen6683);
        if(rdata.time !=0)
        {
          char a[10] = {0};
          
          offset = offset + Reclen6683;
          reversebytes_uint64t(&rdata.time);
          inttou64(a, rdata.time);
          reversebytes_uint32t(&rdata.inactive_time);
          reversebytes_uint32t(&rdata.rx_bytes);
          reversebytes_uint32t(&rdata.rx_packets);
          reversebytes_uint32t(&rdata.tx_bytes);
          reversebytes_uint32t(&rdata.tx_packets);
          reversebytes_uint32t(&rdata.tx_retries);
          reversebytes_uint32t(&rdata.tx_failed);
          reversebytes_uint32t(&rdata.connected_time);
          reversebytes_uint32t(&rdata.signal);
          reversebytes_uint32t(&rdata.signal_avg);
          reversebytes_uint32t(&rdata.expected_throughput);
          expected_throughput_tmp = rdata.expected_throughput;
          expected_throughput = (float)expected_throughput_tmp / 1000.0;
          a[9] = '\0';
          mac_tranADDR_toString_r(rdata.mac_addr, mac_addr, 18);
          memset(insert_data, 0, 500);
          sprintf(insert_data, "INSERT INTO iw(time, mac_addr, device, inactive_time, rx_bytes, rx_packets,tx_bytes, tx_packets, tx_retries, tx_failed, signel, signal_avg, tx_bitrate,rx_bitrate, expected_throughput, connected_time) VALUES(%llu, \"%.18s\", \"%s\", %d, %d, %d, %d,%d, %d, %d, %d, %d, \"%s\", \"%s\", %f, %d)",rdata.time, mac_addr, rdata.device, rdata.inactive_time, rdata.rx_bytes, rdata.rx_packets, rdata.tx_bytes, rdata.tx_packets, rdata.tx_retries, rdata.tx_failed, rdata.signal, rdata.signal_avg, rdata.tx_bitrate, rdata.rx_bitrate, expected_throughput,  rdata.connected_time);
          // insert_data[strlen(insert_data)] = '\0';
          insert_data[strlen(insert_data)] = '\0';
          written_to_mysql_iw++;
          if (key == 0)
          {
            memset(mac_addr, 0, sizeof(mac_addr));
            strncpy(ringbuff6683[write_end6683], insert_data, strlen(insert_data));
            key = 1;
          }
          write_end6683++;
          write_end6683 = write_end6683 % buffer_size_class1;
          write_to_public_buffer(ringbuff6683, &write_start6683, write_end6683);
        }
        else
          offset = 2* Win6683;
      }
      usleep(10000);
    }
}

static void *receive6666(void *arg)
{
  const int Port6666 = 6666;
  const int Win6666 = 1024;
  const int Reclen6666 = 48;

  struct data_winsize {
    __be32 ip_src;
    __be32 ip_dst;
    __be16 sourceaddr;
    __be16 destination;
    __be32 sequence;
    __be32 ack_sequence;
    __be16 flags;
    __be16 windoswsize;
    __be64 systime;
    char wscale[3];
    char mac_addr[13];
  };
  int data_amount = 0;
  unsigned char *ptr_uc;
  char ip_src[20] = { 0 };
  char ip_dst[20] = { 0 };
  char srcportstr[10] = { 0 };
  char dstportstr[10] = { 0 };
  __u32 wanip;
  struct sockaddr_in addr;
  int sockfd, len = 0;    
  int addr_len = sizeof(struct sockaddr_in);
  char buffer[Win6666];  
  struct data_winsize rdata;
  int read_end = 0;
  int offset = 0;
  int res, kind, length, wscale, cal_windowsize;
  __u64 time;
  char mac_addr[18];
  // int step_commit = 0;
  // int tmp;
  char * str;
  str = malloc(sizeof((unsigned char *)&wanip) + sizeof((unsigned char *)&rdata.ip_src) + sizeof((unsigned char *)&rdata.ip_dst) + sizeof((unsigned char *)&rdata.sourceaddr) + sizeof((unsigned char *)&rdata.destination));
  hash_table_init();

  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
      perror ("socket");
      exit(1);
  }
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(Port6666);
  addr.sin_addr.s_addr = htonl(INADDR_ANY) ;
  if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr))<0) {
      perror("connect");
      exit(1);
  }
  ptr_uc = malloc(sizeof(__be32));
  while(1) 
    { 
      char insert_data[500];
      memset(buffer, 0, sizeof(buffer));
      len = recvfrom(sockfd, buffer, sizeof(buffer), 0, 
                     (struct sockaddr *)&addr ,&addr_len);
      read_end = Win6666;
      if (len!=-1)
        packet6666++;
      offset = 0;
      pthread_mutex_lock(&mutex);
      while((read_end > (offset + Reclen6666)))
      {
        int key = 0;
        memcpy(&rdata, buffer+offset, Reclen6666);
        if(rdata.systime !=0)
        {
          

          offset = offset + Reclen6666;
          reversebytes_uint32t(&rdata.ip_src);
          reversebytes_uint32t(&rdata.ip_dst);
          reversebytes_uint16t(&rdata.sourceaddr);
          reversebytes_uint16t(&rdata.destination);
          reversebytes_uint32t(&rdata.sequence);
          reversebytes_uint32t(&rdata.ack_sequence);
          reversebytes_uint16t(&rdata.flags);
          reversebytes_uint16t(&rdata.windoswsize);
          reversebytes_uint64t(&rdata.systime);
          if (key == 0)
          {
            memset(mac_addr, 0, sizeof(mac_addr));
            mac_tranADDR_toString_r(rdata.mac_addr, mac_addr, 18);
            key == 1;
          }
          
          ptr_uc = (unsigned char *)&rdata.ip_src;
          sprintf(ip_src,"%u.%u.%u.%u", ptr_uc[3], ptr_uc[2], ptr_uc[1], ptr_uc[0]);
          ptr_uc = (unsigned char *)&rdata.ip_dst;
          sprintf(ip_dst,"%u.%u.%u.%u", ptr_uc[3], ptr_uc[2], ptr_uc[1], ptr_uc[0]);
          kind = (int)(rdata.wscale[0]);
          length = (int) rdata.wscale[1];
          wscale = (int) rdata.wscale[2];

          rdata.flags = rdata.flags & 0x0017;
          cal_windowsize = rdata.windoswsize;
          u16tostr(rdata.sourceaddr, srcportstr);
          u16tostr(rdata.destination, dstportstr);
          
          if (rdata.flags == 2 || rdata.flags == 18)
          {
              sprintf(str, "%s%s%s%s%s", inet_ntoa(addr.sin_addr), ip_src, ip_dst, srcportstr, dstportstr);
              // printf("insert%s\n", str);
              time = getcurrenttime();
              hash_table_insert(str, wscale, time);
          }
          else if (rdata.flags == 17 || rdata.flags & 0x0004 == 1)
          {
              // printf("rdata.flags is %u\n", rdata.flags);
              sprintf(str, "%s%s%s%s%s", inet_ntoa(addr.sin_addr), ip_src, ip_dst, srcportstr, dstportstr);
              // printf("del%s\n", str);
              if (hash_table_lookup(str) != NULL)
              {
                  hash_table_remove(str);
              }
              sprintf(str, "%s%s%s%s%s", inet_ntoa(addr.sin_addr), ip_dst, ip_src, dstportstr, srcportstr);
              // printf("del%s\n", str);
              if (hash_table_lookup(str) != NULL)
              {
                  hash_table_remove(str);
              }
          }

          else if (rdata.flags == 16)
          {
              time = getcurrenttime();
              sprintf(str, "%s%s%s%s%s", inet_ntoa(addr.sin_addr), ip_src, ip_dst, srcportstr, dstportstr);
              // printf("cal_windowsize %s\n", str);
              if (hash_table_lookup(str) != NULL)
              {
                  cal_windowsize = rdata.windoswsize << hash_table_lookup(str)->nValue;
                  hash_table_lookup(str)->time = time;
              } 
              else
              {
                  time = getcurrenttime();
                  hash_table_insert(str, wscale, time);
              }               
          }
          memset(insert_data, 0, 500);
          sprintf(insert_data, "INSERT INTO winsize(mac_addr, ip_src, ip_dst, srcport, dstport, sequence, ack_sequence, windowsize, cal_windowsize, systime, flags, kind, length, wscale) VALUES(\"%.18s\", \"%s\",\"%s\", %u, %u, %u, %u, %u, %u, %llu, %u, %u, %u, %u)", mac_addr, ip_src, ip_dst, rdata.sourceaddr, rdata.destination, rdata.sequence, rdata.ack_sequence, rdata.windoswsize, cal_windowsize, rdata.systime, rdata.flags, kind, length, wscale);
          insert_data[strlen(insert_data)] = '\0';
          // pthread_cond_wait(&cond_buf_6666, &mutex_buf_6666);
          // printf("insert_data is %s\n", insert_data);
          written_to_mysql_winsize++;
          // printf("================sizeof insert_data %d\n", sizeof(insert_data));
          // memset(insert_data, 0, strlen(insert_data));
          strncpy(ringbuff6666[write_end6666], insert_data, strlen(insert_data));
          write_end6666++;
          write_end6666 = write_end6666 % buffer_size_class1;
          write_to_public_buffer(ringbuff6666, &write_start6666, write_end6666);

        }
        else
          offset = 2 * Win6666;
      }
      pthread_cond_signal(&cond);
      pthread_mutex_unlock(&mutex);
    }
  free(ptr_uc);
  hash_table_release();
  free(str);
}




static void *insertmysql(void *arg)
{
  while(1)
  {
    int step_commit = 0, res = 0;
    MYSQL my_connection;
    char *ptr = NULL;
    mysql_init(&my_connection);
    if (!mysql_real_connect(&my_connection, "localhost", "root", "lin", "record", 0, NULL, 0)) 
      // {
      //   // printf("Connection success\n");
      // }
    // else
    {
      printf("connection failed\n");
    }
    pthread_mutex_lock(&mutex_public_buff);
    pthread_cond_wait(&cond_public_buff, &mutex_public_buff);
    mysql_query(&my_connection, "set autocommit = 0");
    while(public_buff_size > 10)
    {
      // printf("insertmysql\n");
      ptr = NULL;
      ptr = publicbuff[write_start_public];
      // printf("wocao%s\n", ptr);
      res = mysql_query(&my_connection, ptr);
      if (res)
      {
        fprintf(stderr, "insert error %d: %s\n", mysql_errno(&my_connection), mysql_error(&my_connection));
        printf("ptr is %s write_end_public %d write_start_public %d\n", ptr, write_end_public, write_start_public);
        exit(1);
      }      
      write_start_public++;
      write_start_public = write_start_public % buffer_size_public;
      memset(ptr, 0, 500);
      public_buff_size--;
      written_to_mysql++;
      step_commit += 1;
      step_commit  = step_commit % 1000000;
      if (!(step_commit %2000))
      {
        mysql_query(&my_connection, "commit");
        mysql_query(&my_connection, "set autocommit = 1");
        mysql_query(&my_connection, "set autocommit = 0");
      }
    }
    mysql_query(&my_connection, "set autocommit = 1");
    pthread_mutex_unlock(&mutex_public_buff);
    mysql_close(&my_connection);
  }
}


static void *tcp_timeout(void *arg)
{
  int i; 
  __u64 time;
  while(1)
  {
    pthread_mutex_lock(&mutex);
    pthread_cond_wait(&cond, &mutex);
    i = 0; 
    for(i = 0; i < HASH_TABLE_MAX_SIZE; ++i)
    {  
      time = getcurrenttime();
      if(hashTable[i])  
      {  
          HashNode* pHead = hashTable[i];  
          while(pHead)  
          {  
              // printf("duration is %llu, time %llu, pHead->time %llu \n", time - pHead->time, time, pHead->time);
              if ((time - pHead->time) > 120000)
              {
                // printf("remove %s\n", pHead->sKey);
                // printf("size is %d\n", hash_table_size);
                hash_table_remove(pHead->sKey); 
              } 
              pHead = pHead->pNext;  
          }  
          // printf("\n");  
      }
    }    
    pthread_mutex_unlock(&mutex);
    sleep(2);
  }  
}
int main()
{
  
  pthread_t tid1, tid2, tid3, tid4, tid5, tid6, tid7;
  int i = 0, err = 0;
  for (i= 0; i< buffer_size_class1; i++)
  {
    ringbuff6666[i] = (char *)malloc(500);
    ringbuff6666[i][1] = '\0';
    ringbuff6683[i] = (char *)malloc(500);
    ringbuff6683[i][1] = '\0';
    ringbuff6681[i] = (char *)malloc(500);
    ringbuff6681[i][1] = '\0';
    ringbuff6682[i] = (char *)malloc(500);
    ringbuff6682[i][1] = '\0';
  }
  for (i = 0; i < buffer_size_public; i++)
    publicbuff[i] = (char *)malloc(500);
  // cbuf_init(&cmd);


  pthread_cond_init(&cond, NULL);
  pthread_cond_init(&cond_public_buff, NULL);
  pthread_mutex_init(&mutex, NULL);
  pthread_mutex_init(&mutex_public_buff, NULL);
  err = pthread_create(&tid1, NULL, receive6682, NULL);
  if(err != 0)
  {
    printf("receive6682%d creation failed \n", i);
    exit(1);
  }
  err = pthread_create(&tid2, NULL, receie6681, NULL);
  if(err != 0)
  {
    printf("receive6681%d creation failed \n", i);
    exit(1);
  }
  err = pthread_create(&tid3, NULL, receive6683, NULL);
  if(err != 0)
  {
    printf("receive6683%d creation failed \n", i);
    exit(1);
  }
  err = pthread_create(&tid4, NULL, receive6666, NULL);
  if(err != 0)
  {
    printf("receive6666%d creation failed \n", i);
    exit(1);
  }
  err = pthread_create(&tid5, NULL, insertmysql, NULL);
  if(err != 0)
  {
    printf("insertmysql%d creation failed \n", i);
    exit(1);
  }
  err = pthread_create(&tid6, NULL, tcp_timeout, NULL);
  if(err != 0)
  {
    printf("tcp_timeout%d creation failed \n", i);
    exit(1);
  }

  for (i= 0; i< buffer_size_class1; i++)
  {
    free(ringbuff6666[i]);
    free(ringbuff6683[i]);
    free(ringbuff6681[i]);
    free(ringbuff6682[i]);
  }
  for (i = 0; i < buffer_size_public; i++)
    free(publicbuff[i]);

  pthread_exit(0);
  pthread_cond_destroy(&cond);
  pthread_cond_destroy(&cond_public_buff);
  pthread_mutex_destroy(&mutex_public_buff);
  // cbuf_destroy(&cmd);
  return 0;
}