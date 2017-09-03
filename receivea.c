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
#include "structs_lin.h"

#define buffer_size_class1 100000
#define buffer_size_public 1000000
#define BUFFER_STORE 10000 

//used only for debug
int written_to_mysql = 0;
int packet6682 = 0, packet6681 = 0, packet6683 = 0, packet6666 = 0, packet6026 = 0;
int written_to_mysql_iw = 0, written_to_mysql_beacon = 0, written_to_mysql_queue = 0, written_to_mysql_winsize = 0, written_to_mysql_drop = 0;
const int write_threshold = 5000;
// mutex is used to control the hash process
//mutex_public_buff is used to control the writting process of data to public buff
pthread_mutex_t mutex, mutex_public_buff; 
pthread_cond_t cond, cond_public_buff;

//four buffer for four threads to store their data
char *ringbuff6666[buffer_size_class1];
char *ringbuff6682[buffer_size_class1];
char *ringbuff6681[buffer_size_class1];
char *ringbuff6683[buffer_size_class1];
char *ringbuff6026[buffer_size_class1];
//The publicbuff is a bridge of mysql and the four private buffer
char *publicbuff[buffer_size_public];
//8 variables to control the reading and write process of private buffers
int write_start6666 = 0, write_end6666 = 0, buffer_size6666 = 0;
int write_start6682 = 0, write_end6682 = 0, buffer_size6682 = 0;
int write_start6681 = 0, write_end6681 = 0, buffer_size6681 = 0;
int write_start6683 = 0, write_end6683 = 0, buffer_size6683 = 0;
int write_start6026 = 0, write_end6026 = 0, buffer_size6026 = 0;
// public control variables
int write_start_public = 0, write_end_public = 0;
int public_buff_size = 0;

struct data_beacon_processed *beacon_buffer[BUFFER_STORE];
struct data_iw_processed *iw_buffer[BUFFER_STORE];
struct data_queue_processed *queue_buffer[BUFFER_STORE];
struct data_winsize_processed *winsize_buffer[BUFFER_STORE];
struct data_dropped_processed *dropped_buffer[BUFFER_STORE];
int bbstart = 0, bbend = 0, bbamount = 0, bbstart1 = 0, bbamount1 = 0;
int ibstart = 0, ibend = 0, ibamount = 0, ibstart1 = 0, ibamount1 = 0;
int qbstart = 0, qbend = 0, qbamount = 0, qbstart1 = 0, qbamount1 = 0;
int wbstart = 0, wbend = 0, wbamount = 0, wbstart1 = 0, wbamount1 = 0;
int dbstart = 0, dbend = 0, dbamount = 0, dbstart1 = 0, dbamount1 = 0;
//used to get current time, it is used to control life cycle of tcp connection

static __u64 getcurrenttime(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

//big endian to small endian
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
        temp += ((str[i-1]-0x30)*fact);
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
//used to tranform 04a151a3571a to 04:a1:51:a3:57:1a
void mac_tranADDR_toString_r(unsigned char* addr, char* str, size_t size)
{
    if(addr == NULL || str == NULL || size < 18)
      exit(1);

    snprintf(str, size, "%02x:%02x:%02x:%02x:%02x:%02x", 
             addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
    str[17] = '\0';
}


//aggarevate private to public, and then write to mysql
void write_to_public_buffer(char **buffer, int *write_begin, int *buff_size)
{
  if (!((written_to_mysql_beacon + written_to_mysql_queue + written_to_mysql_iw + written_to_mysql_winsize + written_to_mysql_drop) % 100000))
  {
    printf("all the packets is %d 6666 %d 6682 %d 6681 %d 6683  %d 6026 %d public_buff_size %d, \n",packet6682 + packet6681 + packet6683 + packet6666, packet6666, packet6682, packet6681, packet6683, packet6026, public_buff_size);
    printf(" written_to_mysql %d all %d beacon %d iw %d queue %d winsize %d drop_driver %d\n", written_to_mysql, (written_to_mysql_beacon + written_to_mysql_queue + written_to_mysql_iw + written_to_mysql_winsize), written_to_mysql_beacon, written_to_mysql_iw, written_to_mysql_queue, written_to_mysql_winsize, written_to_mysql_drop);
  }

  // mutex public buff for writing
  pthread_mutex_lock(&mutex_public_buff);
  while(*buff_size > write_threshold)
  {
    memset(publicbuff[write_end_public], 0, 600);
    strcpy(publicbuff[write_end_public], buffer[*write_begin]);
    // printf("after strlen %d %s write_end_public %d\n",strlen(buffer[*write_begin]), publicbuff[write_end_public], write_end_public);
    write_end_public = write_end_public + 1;
    write_end_public = write_end_public % buffer_size_public;
    public_buff_size = public_buff_size + 1;
    *write_begin = *write_begin + 1;
    *buff_size = *buff_size - 1;
    *write_begin = *write_begin % buffer_size_class1;
  }
  if (public_buff_size > 4000)
  {
    pthread_cond_signal(&cond_public_buff);
  }
  pthread_mutex_unlock(&mutex_public_buff);
}
int get_aps(char (*mac_list)[18])
{
  int begin = 0;
  int end = 0, found = 0;
  int i = 0, length = 0, j = 0, mac_list_last = 0;
  
  int amount = 0;
  struct aplist *neibours;

  struct data_beacon_processed *beacon_tmp[BUFFER_STORE];
  for(i = 0; i < BUFFER_STORE; i++)
    beacon_tmp[i] = (struct data_beacon_processed *)malloc(sizeof(struct data_beacon_processed));
  i = 0;
  begin = bbstart;
  end = bbend;
  for(i = 0; i < BUFFER_STORE; i++)
  {
    memcpy(beacon_tmp[i], beacon_buffer[i], sizeof(struct data_beacon_processed));
  }

  if(end >= begin)
    length = end - begin;
  else
    length = end + BUFFER_STORE - begin;
  j = begin;
  // printf("length is %d bbamount %d bbstart %d bbend %d\n", length, bbamount, bbstart, bbend);
  for(i = 0; i < length; i++)
  {
    found = 0;
    // printf("%s\n", beacon_tmp[j]->mac_addr); 
    bbstart = bbstart + 1;
    bbstart = bbstart % BUFFER_STORE;
    bbamount = bbamount - 1;
    if(bbamount < 0)
    {
      bbamount = 0;
    }
    
    if(mac_list_last > 0)
    {
      int m = 0;
      for(m = 0; m < mac_list_last; m++)
      {
        if(!strcmp(mac_list[m], beacon_tmp[j]->mac_addr))
          {
            found = 1;
            // j = j + 1;
            // j = j % BUFFER_STORE;
            break;
          }
      }
      if(found == 1)
      {
        j = j + 1;
        j = j % BUFFER_STORE;
        continue;
      }
    }
    strcpy(mac_list[mac_list_last], beacon_tmp[j]->mac_addr);
    amount = amount + 1;
    mac_list_last = mac_list_last + 1;
    j = j + 1;
    j = j % BUFFER_STORE;
  }

  i = 0;
  for(i = 0; i < BUFFER_STORE; i++)
  {
    if(beacon_tmp[i] != NULL)
      free(beacon_tmp[i]);
  }
  return amount;
}

void *get_neighbour(char *ap, struct aplist *neibours)
{
  int begin = 0;
  int end = 0, found = 0;
  int i = 0, length = 0, j = 0, mac_list_last = 0;
  char mac_list[20][18];
  

  struct data_beacon_processed *beacon_tmp[BUFFER_STORE];
  for(i = 0; i < BUFFER_STORE; i++)
    beacon_tmp[i] = (struct data_beacon_processed *)malloc(sizeof(struct data_beacon_processed));
  i = 0;
  begin = bbstart1;
  end = bbend;
  for(i = 0; i < BUFFER_STORE; i++)
  {
    memcpy(beacon_tmp[i], beacon_buffer[i], sizeof(struct data_beacon_processed));
  }
  

  if(end >= begin)
    length = end - begin;
  else
    length = end + BUFFER_STORE - begin;
  j = begin;
  // printf("length is %d bbamount1 %d bbstart1 %d bbend %d\n", length, bbamount1, bbstart1, bbend);
  // for(i = 0; i < length; i++)
  // {
  //   printf("%s strlen %d\n", beacon_tmp[j]->mac_addr, strlen(beacon_tmp[j]->mac_addr));
  //   j = j + 1;
  //   j = j % BUFFER_STORE;
  //   bbstart1 = bbstart1 + 1;
  //   bbstart1 = bbstart1 % BUFFER_STORE;
  //   bbamount1 = bbamount1 - 1;
  //   if(bbamount1 < 0)
  //   {
  //     bbamount1 = 0;
  //   }
  // }

  for(i = 0; i < length; i++)
  {
    bbstart1 = bbstart1 + 1;
    bbstart1 = bbstart1 % BUFFER_STORE;
    bbamount1 = bbamount1 - 1;
    if(bbamount1 < 0)
    {
      bbamount1 = 0;
    }
    // printf("%s\n", beacon_tmp[j]->mac_addr);
    found = 0;
    if(strcmp(beacon_tmp[j]->mac_addr, ap) == 0)
    {
      if(mac_list_last > 0)
      {
        int m = 0;
        for(m = 0; m < mac_list_last; m++)
        {
          if(!strcmp(mac_list[m], beacon_tmp[j]->bssid))
            {
              found = 1;
              break;
            }
        }
      }
      if(found == 1)
      {
        j = j + 1;
        j = j % BUFFER_STORE;
        continue;
      }
      // printf("hereeee\n");
      strcpy(mac_list[mac_list_last], beacon_tmp[j]->bssid);
      mac_list_last = mac_list_last + 1;
      j = j + 1;
      j = j % BUFFER_STORE;
    }
  }
  if(mac_list_last > 0)
  {
    // printf("here\n");
    neibours->length = mac_list_last;
    memcpy(neibours->aplists, mac_list, sizeof(mac_list));
    i = 0;
    for(i = 0; i < BUFFER_STORE; i++)
    {
      free(beacon_tmp[i]);
    }
    // return neibours;
  }
  else
  {
    i = 0;
    for(i = 0; i < BUFFER_STORE; i++)
    {
      free(beacon_tmp[i]);
    }
    // return NULL;
  }
}

void *get_clients(char *ap, struct aplist *clients)
{
  int begin = 0, end = 0, found = 0;
  int i = 0, j = 0, length = 0, mac_list_last = 0;
  struct data_iw_processed *iw_tmp[BUFFER_STORE];
  char mac_list[20][18];
  // aplist *clients;
  for(i = 0; i < BUFFER_STORE; i++)
  {
    iw_tmp[i] = (struct data_iw_processed *)malloc(sizeof(struct data_iw_processed));
  }
  i = 0;
  for(i = 0; i < BUFFER_STORE; i++)
  {
    memcpy(iw_tmp[i], iw_buffer[i], sizeof(struct data_iw_processed));
  }

  begin = ibstart;
  end = ibend;
  if(end >= begin)
  {
    length = end - begin;
  }
  else
  {
    length = end + BUFFER_STORE - begin;
  }
  j = begin;
  // printf("length is %d ibamount %d ibstart %d ibend %d\n", length, ibamount, ibstart, ibend);
  for(i = 0; i < length; i++)
  {
    // printf("%s\n", iw_tmp[j]->station);
    ibstart = ibstart + 1;
    ibstart = ibstart % BUFFER_STORE;
    ibamount = ibamount - 1;
    if(ibamount < 0)
    {
      ibamount = 0;
    }
    found = 0;

    if(strcmp(iw_tmp[j]->mac_addr, ap) == 0)
    {
      if(mac_list_last > 0)
      {
        int m = 0;
        for(m = 0; m < mac_list_last; m++)
        {
          if(!strcmp(mac_list[m], iw_tmp[j]->station))
            {
              found = 1;
              break;
            }
        }
      }
      if(found == 1)
      {
        j = j + 1;
        j = j % BUFFER_STORE;
        continue;
      }
      // printf("hereeee\n");
      strcpy(mac_list[mac_list_last], iw_tmp[j]->station);
      mac_list_last = mac_list_last + 1;
      j = j + 1;
      j = j % BUFFER_STORE;
    }
  }
  // clients = (aplist *)malloc(sizeof(struct aplist));

  clients->length = mac_list_last;
  if(mac_list_last > 0)
    memcpy(clients->aplists, mac_list, sizeof(mac_list));
  for(i = 0; i < BUFFER_STORE; i++)
  {
    free(iw_tmp[i]);
  }
  // free(mac_list);
  // retrun clients;
}

void *get_flows(char *ap, struct flowlist *flowlists)
{
  int begin = 0, end = 0, found = 0;
  int i = 0, j = 0, length = 0, mac_list_last = 0;
  struct data_winsize_processed *winsize_tmp[BUFFER_STORE];
  struct flow flows[40];
  for(i = 0; i < BUFFER_STORE; i++)
  {
    winsize_tmp[i] = (struct data_winsize_processed *)malloc(sizeof(struct data_winsize_processed));
  }
  for(i = 0; i < BUFFER_STORE; i++)
  {
    memcpy(winsize_tmp[i], winsize_buffer[i], sizeof(struct data_winsize_processed));
  }
  i = 0;
  begin = wbstart;
  end = wbend;
  if(end >= begin)
  {
    length = end - begin;
  }
  else
  {
    length = end + BUFFER_STORE - begin;
  }
  j = begin;
  printf("length is %d wbamount %d wbstart %d wbend %d\n", length, wbamount, wbstart, wbend);
  for(i = 0; i < length; i++)
  {
    int m = 0;
    wbstart = wbstart + 1;
    wbstart = wbstart % BUFFER_STORE;
    wbamount = wbamount - 1;
    if(wbamount < 0)
    {
      wbamount = 0;
    }
    found = 0;
    // printf("%d\n", winsize_tmp[j]->ip_src);
    for(m = 0; m < mac_list_last; m++)
    {
      if((winsize_tmp[j]->ip_src == flows[m].ip_src) && (winsize_tmp[j]->ip_dst == flows[m].ip_dst) && (winsize_tmp[j]->sourceaddr == flows[m].port_src) && (winsize_tmp[j]->destination == flows[m].port_dst))
      {
        found = 1;
        break;
      }
    }
    if(found == 1)
    {
      j = j + 1;
      j = j % BUFFER_STORE;
      continue;
    }
    flows[mac_list_last].ip_src = winsize_tmp[j]->ip_src;
    flows[mac_list_last].ip_dst = winsize_tmp[j]->ip_dst;
    flows[mac_list_last].port_src = winsize_tmp[j]->sourceaddr;
    flows[mac_list_last].port_dst = winsize_tmp[j]->destination;
    mac_list_last = mac_list_last + 1;
    j = j + 1;
    j = j % BUFFER_STORE;
  }
  // flowlists = (flowlist *)malloc(sizeof(struct flowlist));
  flowlists->length = mac_list_last;
  // memcpy(flowlists->flows, flows);
  for(i = 0; i < mac_list_last; i++)
  {
    memcpy(&flowlists->flows[i], &flows[i], sizeof(struct flow));
  }
  for(i = 0; i < BUFFER_STORE; i++)
  {
    free(winsize_tmp[i]);
  }
}

void *get_flow_drops(char *ap, struct flow_drop_ap *flow_drops)
{
  int begin = 0, total_drops = 0;
  int end = 0;
  int i = 0, length = 0, j = 0, mac_list_last = 0;
  struct flow_and_dropped mac_list[50];
  

  struct data_dropped_processed *dropped_tmp[BUFFER_STORE];
  for(i = 0; i < BUFFER_STORE; i++)
    dropped_tmp[i] = (struct data_dropped_processed *)malloc(sizeof(struct data_dropped_processed));
  for(i = 0; i < BUFFER_STORE; i++)
  {
    memcpy(dropped_tmp[i], dropped_buffer[i], sizeof(struct data_dropped_processed));
  }
  i = 0;
  begin = dbstart;
  end = dbend;

  if(end >= begin)
    length = end - begin;
  else
    length = end + BUFFER_STORE - begin;
  j = begin;
  for(i = 0; i < length; i++)
  {
    dbstart = dbstart + 1;
    dbstart = dbstart % BUFFER_STORE;
    dbamount = dbamount - 1;
    if(dbamount < 0)
    {
      dbamount = 0;
    }
    if(strcmp(dropped_buffer[j]->mac_addr, ap) == 0)
    {
      int k = 0;
      for (k = 0; k < mac_list_last; k++)
      {
        bool condition = 0;
        condition = (dropped_tmp[j]->ip_src == mac_list[k].dataflow.ip_src) && (dropped_tmp[j]->ip_dst == mac_list[k].dataflow.ip_dst);
        condition = condition && ((dropped_tmp[j]->port_src == mac_list[k].dataflow.port_src) && (dropped_tmp[j]->port_dst == mac_list[k].dataflow.port_dst));
        if(condition)
        {
          mac_list[k].drops = mac_list[k].drops + 1;
          total_drops = total_drops + 1;
        }
      }
      j = j + 1;
      j = j % BUFFER_STORE;
    }
    else
    {
      j = j + 1;
      j = j % BUFFER_STORE;
      continue;
    }
  }
  flow_drops->drops_total = total_drops;
  flow_drops->length = mac_list_last;
  for(i = 0; i < mac_list_last; i++)
  {
    memcpy(&flow_drops->flowinfo[i], &mac_list[i], sizeof(struct flow_and_dropped));
  }
}

bool get_wireless_info(char *ap, struct wireless_information_ap *clients)
{
  int begin = 0, end = 0;
  bool found = 0;
  int i = 0, j = 0, length = 0, mac_list_last = 0;
  struct data_iw_processed *iw_tmp[BUFFER_STORE];
  // aplist *clients;
  for(i = 0; i < BUFFER_STORE; i++)
  {
    iw_tmp[i] = (struct data_iw_processed *)malloc(sizeof(struct data_iw_processed));
  }
  i = 0;
  for(i = 0; i < BUFFER_STORE; i++)
  {
    memcpy(iw_tmp[i], iw_buffer[i], sizeof(struct data_iw_processed));
  }

  begin = ibstart1;
  end = ibend;
  if(end >= begin)
  {
    length = end - begin;
  }
  else
  {
    length = end + BUFFER_STORE - begin;
  }
  j = begin;
  // printf("length is %d ibamount %d ibstart %d ibend %d\n", length, ibamount, ibstart, ibend);
  for(i = 0; i < length; i++)
  {
    // printf("%llu\n", iw_tmp[j]->active_time);
    ibstart1 = ibstart1+ 1;
    ibstart1 = ibstart1 % BUFFER_STORE;
    ibamount1 = ibamount1 - 1;
    if(ibamount1 < 0)
    {
      ibamount1 = 0;
    }
    if(strcmp(iw_tmp[j]->mac_addr, ap) == 0)
    {
      clients->noise = iw_buffer[j]->noise;
      clients->connected_time = iw_buffer[j]->connected_time;
      clients->active_time = iw_buffer[j]->active_time;
      clients->busy_time = iw_buffer[j]->busy_time;
      clients->transmit_time = iw_buffer[j]->transmit_time;
      clients->receive_time = iw_buffer[j]->receive_time;
      found = 1;
      break;
    }
    j = j + 1;
    j = j % BUFFER_STORE;
  }
  // clients = (aplist *)malloc(sizeof(struct aplist));

  for(i = 0; i < BUFFER_STORE; i++)
  {
    free(iw_tmp[i]);
  }
  return found;
}


static void *receive6682(void *arg)
{
  const int Win6682 = 2048;

  //socket related
  int sockfd; 
  int len;
  struct sockaddr_in addr;
  int addr_len = sizeof(struct sockaddr_in);
  //buffer related
  char buffer[Win6682];  //readbuffer
  int offset = 0; //offset of read

  //tmp variables
  struct data_beacon beacon; 
  signed char data_rate;
  int current_channel, ssl_signal;
  char channel_type[] = "802.11a";
  char mac_addr_beacon[18], bssid[18];
  char insert_data[600];
  //connect socket
  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
      perror ("socket");
      exit(1);
  }
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(6682);
  addr.sin_addr.s_addr = htonl(INADDR_ANY) ;// 接收任意IP发来的数据

  if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr))<0) {
      perror("connect");
      exit(1);
  }

  while(1) 
    { 
      memset(&beacon, 0, sizeof(beacon));
      memset(buffer, 0, sizeof(buffer));
      len = recvfrom(sockfd, buffer, sizeof(buffer), 0, 
                     (struct sockaddr *)&addr ,&addr_len);

      if (len != -1)
        packet6682 += 1;
      offset = 0;
      while((Win6682 > (offset + Reclen6682)))
      {
        int i = 0;
        int key = 0; //used to get mac address once for every packet
        memcpy(&beacon, buffer+offset, Reclen6682);
        if(beacon.time_current !=0)
        {
          offset = offset + Reclen6682;
          reversebytes_uint64t(&beacon.time_current);
          ssl_signal = (signed char)beacon.ssl_signal[0];
          current_channel = (int)beacon.current_channel[1] * 256 + (int)beacon.current_channel[0];
          data_rate = (int)beacon.data_rate[0];
          if (key == 0)
          {
            // memset(mac_addr_beacon, 0, strlen(mac_addr_beacon));
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

          struct data_beacon_processed *tmp;
          tmp = beacon_buffer[bbend];
          strcpy(tmp->mac_addr, mac_addr_beacon);
          tmp->data_rate = data_rate;
          tmp->current_channel = current_channel;
          strcpy(tmp->channel_type, channel_type);
          tmp->ssl_signal = ssl_signal;
          strcpy(tmp->bssid, bssid);
          tmp->time_current = beacon.time_current;
          bbamount = bbamount + 1;
          bbamount1 = bbamount1 + 1;
          bbend = bbend + 1;
          bbend = bbend % BUFFER_STORE;
          if(bbamount > BUFFER_STORE)
          {
            bbstart = bbend + 1;
            bbstart = bbstart % BUFFER_STORE;
            bbamount = BUFFER_STORE;
          }
          if(bbamount1 > BUFFER_STORE)
          {
            bbstart1 = bbend + 1;
            bbstart1 = bbstart1 % BUFFER_STORE;
            bbamount1 = BUFFER_STORE;
          }


          memset(insert_data, 0, 600);
          sprintf(insert_data, "INSERT INTO beacon(time, data_rate, current_channel, channel_type, ssl_signal, bw, bssid, mac_addr) VALUES(%llu, %d, %d, \"%s\", %d, %d, \"%.18s\", \"%.18s\")\0", beacon.time_current, data_rate, current_channel, channel_type, ssl_signal, 0,  bssid, mac_addr_beacon);
          insert_data[strlen(insert_data)] = '\0';
          written_to_mysql_beacon += 1;
          strcpy(ringbuff6682[write_end6682], insert_data);
          // strncpy(ringbuff6682[write_end6682], insert_data, strlen(insert_data));
          write_end6682 = write_end6682 + 1;
          write_end6682 = write_end6682 % buffer_size_class1;
          buffer_size6682 = buffer_size6682 + 1;
          if (buffer_size6682 > write_threshold)
            write_to_public_buffer(ringbuff6682, &write_start6682, &buffer_size6682);
        }
        else
          offset = 2 * Win6682;
      }
      usleep(10000);
    }
}
static void *receie6681(void *arg)
{
  const int Win6681 = 2048;
  int queue_id;


  struct sockaddr_in addr;
  int sockfd, len = 0;    
  int addr_len = sizeof(struct sockaddr_in);

  char buffer[Win6681];  
  struct data_queue rdata;
  int offset = 0;

  char mac_addr[18];
  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
      perror ("socket");
      exit(1);
  }
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(6681);
  addr.sin_addr.s_addr = htonl(INADDR_ANY) ;

  if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr))<0) {
      perror("connect");
      exit(1);
  }

  while(1) 
    { 
      char insert_data[600];
      
      memset(buffer, 0, sizeof(buffer));
      len = recvfrom(sockfd, buffer, sizeof(buffer), 0, 
                     (struct sockaddr *)&addr ,&addr_len);

      if (len != -1)
        packet6681 += 1;
      offset = 0;
       // printf("$$$$$write_end6681 %d write_start6681 %d\n", write_end6681, write_start6681);
      while((Win6681 > (offset + Reclen6681)))
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
          // reversebytes_uint32t(&rdata.overlimits);
          // reversebytes_uint32t(&rdata.deficit);
          // reversebytes_uint32t(&rdata.ldelay);
          // reversebytes_uint32t(&rdata.count);
          // reversebytes_uint32t(&rdata.lastcount);
          // reversebytes_uint32t(&rdata.dropping);
          // reversebytes_uint32t(&rdata.drop_next);
          //get the mac
          if (key == 0)
          {
            // memset(mac_addr, 0, sizeof(mac_addr));
            mac_tranADDR_toString_r(rdata.mac_addr, mac_addr, 18);
            key = 1;
          }
          // printf("%u\n", rdata.queue_id);
          // queue_id = rdata.queue_id;

          struct data_queue_processed *tmp;
          tmp = queue_buffer[qbend];
          tmp->time = rdata.time;
          tmp->queue_id = rdata.queue_id;
          tmp->bytes = rdata.bytes;
          tmp->packets = rdata.packets;
          tmp->qlen = rdata.qlen;
          tmp->backlog = rdata.backlog;
          tmp->drops = rdata.drops;
          tmp->requeues = rdata.requeues;
          strcpy(tmp->mac_addr, mac_addr);

          qbend = qbend + 1;
          qbend = qbend % BUFFER_STORE;

          memset(insert_data, 0, 600);
          sprintf(insert_data, "INSERT INTO queue(mac_ddr, time, queue_id, bytes, packets, qlen, backlog, drops, requeues) VALUES(\"%.18s\", %llu, %u, %llu, %u, %u, %u, %u, %u)\0", mac_addr, rdata.time, rdata.queue_id, rdata.bytes, rdata.packets, rdata.qlen,rdata.backlog, rdata.drops, rdata.requeues);
          insert_data[strlen(insert_data)] = '\0';
          written_to_mysql_queue += 1;
          // memcpy(ringbuff6681[write_end6681], insert_data, strlen(insert_data));
          strcpy(ringbuff6681[write_end6681], insert_data);
          // if (strlen(insert_data) != strlen(ringbuff6681[write_end6681]))
          // {
          //   // printf("write_end6681 %d write_start6681 %d\n", write_end6681, write_start6681);
          //   // printf("before strlen %d %d %s write_end6681 %d\n", strlen(insert_data), strlen(ringbuff6681[write_end6681]), insert_data, write_end6681);
          //   // printf("insert_data %s\n", insert_data);
          //   // printf("after copy %s\n", ringbuff6681[write_end6681]);
          //   exit(1);
          // }
          buffer_size6681 += 1;
          write_end6681 += 1;
          write_end6681 = write_end6681 % buffer_size_class1;
          if (buffer_size6681 > write_threshold)
            write_to_public_buffer(ringbuff6681, &write_start6681, &buffer_size6681);            
        }
        else
          offset = 2* Win6681;
      }
      usleep(1000);
    }

}
static void *receive6683(void *arg)
{
  const int Win6683 = 1024;


  struct sockaddr_in addr;
  int sockfd, len = 0;    
  int addr_len = sizeof(struct sockaddr_in);
  char buffer[Win6683]; 
  struct data_iw rdata;
  int offset = 0;

  __u32 expected_throughput_tmp;
  float expected_throughput;


  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
      perror ("socket");
      exit(1);
  }
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(6683);
  addr.sin_addr.s_addr = htonl(INADDR_ANY) ;

  if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr))<0) {
      perror("connect");
      exit(1);
  }

  while(1) 
    { 
      char insert_data[600];
      char mac_addr[18];
      char station[18];

      memset(buffer, 0, sizeof(buffer));
      len = recvfrom(sockfd, buffer, sizeof(buffer), 0, 
                     (struct sockaddr *)&addr ,&addr_len);

      offset = 0;
      if (len != -1)
        packet6683 += 1;
      while((Win6683 > (offset + Reclen6683)))
      {
        int key = 0;
        memcpy(&rdata, buffer+offset, Reclen6683);
        if(rdata.time !=0)
        {
          offset = offset + Reclen6683;
          reversebytes_uint64t(&rdata.time);
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
          reversebytes_uint32t(&rdata.noise);
          reversebytes_uint64t(&rdata.active_time);
          reversebytes_uint64t(&rdata.busy_time);
          reversebytes_uint64t(&rdata.receive_time);
          reversebytes_uint64t(&rdata.transmit_time);
          expected_throughput_tmp = rdata.expected_throughput;
          expected_throughput = (float)expected_throughput_tmp / 1000.0;
          mac_tranADDR_toString_r(rdata.mac_addr, mac_addr, 18);
          if (key == 0)
          {
            memset(mac_addr, 0, sizeof(mac_addr));
            mac_tranADDR_toString_r(rdata.mac_addr, mac_addr, 18);
            key = 1;
          }
          mac_tranADDR_toString_r(rdata.station, station, 18);

          struct data_iw_processed *tmp;
          tmp = iw_buffer[ibend];
          tmp->time = rdata.time;
          strcpy(tmp->mac_addr, mac_addr);
          strcpy(tmp->station, station);
          strcpy(tmp->device, rdata.device);
          tmp->inactive_time = rdata.inactive_time;
          tmp->rx_bytes = rdata.rx_bytes;
          tmp->rx_packets = rdata.rx_packets;
          tmp->tx_bytes = rdata.tx_bytes;
          tmp->tx_packets = rdata.tx_packets;
          tmp->tx_retries = rdata.tx_retries;
          tmp->tx_failed = rdata.tx_failed;
          tmp->signal = rdata.signal;
          tmp->signal_avg = rdata.signal_avg;
          tmp->noise = rdata.noise;
          tmp->expected_throughput = expected_throughput;
          tmp->active_time = rdata.active_time;
          tmp->connected_time = rdata.connected_time;
          tmp->busy_time = rdata.busy_time;
          tmp->receive_time = rdata.receive_time;
          tmp->transmit_time = rdata.transmit_time;
          ibamount = ibamount + 1;
          ibamount1 = ibamount1 + 1;
          ibend = ibend + 1;
          ibend = ibend % BUFFER_STORE;
          if(ibamount > BUFFER_STORE)
          {
            ibstart = ibend + 1;
            ibstart = ibstart % BUFFER_STORE;
            ibamount = BUFFER_STORE;
          }
          if(ibamount1 > BUFFER_STORE)
          {
            ibstart1 = ibend + 1;
            ibstart1 = ibstart1 % BUFFER_STORE;
            ibamount1 = BUFFER_STORE;
          }

          memset(insert_data, 0, 600);
          sprintf(insert_data, "INSERT INTO iw(time, station, mac_addr, device, inactive_time, rx_bytes, rx_packets,tx_bytes, tx_packets, tx_retries, tx_failed, signel, signal_avg, noise, expected_throughput, connected_time, active_time, busy_time, receive_time, transmit_time) VALUES(%llu, \"%.18s\", \"%.18s\", \"%s\", %u, %u, %u, %u, %u, %u, %u, %d, %d, %d, %f, %u, %llu, %llu, %llu, %llu)\0",rdata.time, station, mac_addr, rdata.device, rdata.inactive_time, rdata.rx_bytes, rdata.rx_packets, rdata.tx_bytes, rdata.tx_packets, rdata.tx_retries, rdata.tx_failed, rdata.signal, rdata.signal_avg, rdata.noise, expected_throughput,  rdata.connected_time, rdata.active_time, rdata.busy_time, rdata.receive_time, rdata.transmit_time);
          insert_data[strlen(insert_data)] = '\0';
          // printf("%d\n", strlen(insert_data));
          strcpy(ringbuff6683[write_end6683], insert_data);
          written_to_mysql_iw += 1;
          write_end6683 += 1;
          write_end6683 = write_end6683 % buffer_size_class1;
          buffer_size6683 += 1;
          if (buffer_size6683 > write_threshold)
            write_to_public_buffer(ringbuff6683, &write_start6683, &buffer_size6683);
        }
        else
          offset = 2* Win6683;
      }
      usleep(1000);
    }
}


static void *receive6666(void *arg)
{
  const int Win6666 = 1024;
  char insert_data[600];
  // FILE *fp;
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

  int offset = 0;
  int res, kind, length, wscale, cal_windowsize;
  __u64 time; //used to timeout hash table
  char mac_addr[18], mac_addr_origin[6], eth_src[18], eth_dst[18];

  char * str;
  str = malloc(sizeof((unsigned char *)&wanip) + sizeof((unsigned char *)&rdata.ip_src) + sizeof((unsigned char *)&rdata.ip_dst) + sizeof((unsigned char *)&rdata.sourceaddr) + sizeof((unsigned char *)&rdata.destination));
  hash_table_init();

  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
      perror ("socket");
      exit(1);
  }
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(6666);
  addr.sin_addr.s_addr = htonl(INADDR_ANY) ;
  if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr))<0) {
      perror("connect");
      exit(1);
  }
  ptr_uc = malloc(sizeof(__be32));

  // if ((fp = fopen("6666.txt", "wt")) == NULL)
  // {
  //   printf("file open failed\n");
  //   exit(1);
  // }


  while(1) 
    { 
      memset(buffer, 0, sizeof(buffer));
      len = recvfrom(sockfd, buffer, sizeof(buffer), 0, 
                     (struct sockaddr *)&addr ,&addr_len);
      if (len!= -1)
        packet6666 += 1;
      offset = 0;
      pthread_mutex_lock(&mutex);
      while((Win6666 > (offset + Reclen6666)))
      {
        int key = 0;
        memcpy(&rdata, buffer+offset, Reclen6666);
        if(rdata.systime !=0)
        {
          offset = offset + Reclen6666;
          reversebytes_uint32t(&rdata.datalength);
          reversebytes_uint32t(&rdata.ip_src);
          reversebytes_uint32t(&rdata.ip_dst);
          reversebytes_uint16t(&rdata.sourceaddr);
          reversebytes_uint16t(&rdata.destination);
          reversebytes_uint32t(&rdata.sequence);
          reversebytes_uint32t(&rdata.ack_sequence);
          reversebytes_uint16t(&rdata.flags);
          reversebytes_uint16t(&rdata.windowsize);
          reversebytes_uint64t(&rdata.systime);
          if (key == 0)
          {
            memset(mac_addr, 0, sizeof(mac_addr));
            memset(mac_addr_origin, 0, 6);
            mac_tranADDR_toString_r(rdata.mac_addr, mac_addr, 18);
            memcpy(mac_addr_origin, rdata.mac_addr, 6);
            // printf("mac_addr_origin %02x %02x %02x %02x %02x %02x %s\n", mac_addr_origin[0], mac_addr_origin[1], mac_addr_origin[2], mac_addr_origin[3], mac_addr_origin[4], mac_addr_origin[5], mac_addr);
            key == 1;
          }
          mac_tranADDR_toString_r(rdata.eth_src, eth_src, 18);  
          mac_tranADDR_toString_r(rdata.eth_dst, eth_dst, 18);
          ptr_uc = (unsigned char *)&rdata.ip_src;
          sprintf(ip_src,"%u.%u.%u.%u", ptr_uc[3], ptr_uc[2], ptr_uc[1], ptr_uc[0]);
          ptr_uc = (unsigned char *)&rdata.ip_dst;
          sprintf(ip_dst,"%u.%u.%u.%u", ptr_uc[3], ptr_uc[2], ptr_uc[1], ptr_uc[0]);
          kind = (int)(rdata.wscale[0]);
          length = (int) rdata.wscale[1];
          wscale = (int) rdata.wscale[2];

          rdata.flags = rdata.flags & 0x0017;
          cal_windowsize = rdata.windowsize;
          u16tostr(rdata.sourceaddr, srcportstr);
          u16tostr(rdata.destination, dstportstr);
          
          if (rdata.flags == 2 || rdata.flags == 18)
          {
              sprintf(str, "%s%u%u%u%u", mac_addr_origin, rdata.ip_src, rdata.ip_dst, rdata.sourceaddr, rdata.destination);
              // printf("str %s\n", str);
              time = getcurrenttime();
              hash_table_insert(str, wscale, time);
          }
          else if (rdata.flags == 17 || rdata.flags & 0x0004 == 1)
          {
              sprintf(str, "%s%u%u%u%u", mac_addr_origin, rdata.ip_src, rdata.ip_dst, rdata.sourceaddr, rdata.destination);

              if (hash_table_lookup(str) != NULL)
              {
                  hash_table_remove(str);
              }
              sprintf(str, "%s%u%u%u%u", mac_addr_origin, rdata.ip_dst, rdata.ip_src, rdata.destination, rdata.sourceaddr);

              if (hash_table_lookup(str) != NULL)
              {
                  hash_table_remove(str);
              }
          }

          else if (rdata.flags == 16)
          {
              time = getcurrenttime();
              sprintf(str, "%s%u%u%u%u", mac_addr_origin, rdata.ip_src, rdata.ip_dst, rdata.sourceaddr, rdata.destination);

              if (hash_table_lookup(str) != NULL)
              {
                  cal_windowsize = rdata.windowsize << hash_table_lookup(str)->nValue;
                  hash_table_lookup(str)->time = time;
              } 
              else
              {
                  time = getcurrenttime();
                  hash_table_insert(str, wscale, time);
              }               
          }

          struct data_winsize_processed *tmp;
          tmp = winsize_buffer[wbend];
          tmp->ip_src = rdata.ip_src;
          tmp->ip_dst = rdata.ip_dst;
          tmp->sourceaddr = rdata.sourceaddr;
          tmp->destination = rdata.destination;
          tmp->sequence = rdata.sequence;
          tmp->ack_sequence = rdata.ack_sequence;
          tmp->flags = rdata.flags;
          tmp->windowsize = rdata.windowsize;
          tmp->systime = rdata.systime;
          tmp->datalength = rdata.datalength;
          strcpy(tmp->mac_addr, mac_addr);
          strcpy(tmp->eth_src, eth_src);
          strcpy(tmp->eth_dst, eth_dst);
          tmp->kind = kind;
          tmp->length = length;

          wbamount = wbamount + 1;
          wbamount1 = wbamount1 + 1;
          wbend = wbend + 1;
          wbend = wbend % BUFFER_STORE;
          if(wbamount > BUFFER_STORE)
          {
            wbstart = wbend + 1;
            wbstart = wbstart % BUFFER_STORE;
            wbamount = BUFFER_STORE;
          }
          if(wbamount1 > BUFFER_STORE)
          {
            wbstart1 = wbend + 1;
            wbstart1 = wbstart1 % BUFFER_STORE;
            wbamount1 = BUFFER_STORE;
          }

          memset(insert_data, 0, 600);
          sprintf(insert_data, "INSERT INTO winsize(mac_addr, eth_src, eth_dst, ip_src, ip_dst, srcport, dstport, sequence, ack_sequence, windowsize, cal_windowsize, time, datalength, flags, kind, length, wscale) VALUES(\"%.18s\", \"%.18s\", \"%.18s\", \"%s\",\"%s\", %u, %u, %u, %u, %u, %u, %llu, %u, %u, %u, %u, %u)\0", mac_addr, eth_src, eth_dst, ip_src, ip_dst, rdata.sourceaddr, rdata.destination, rdata.sequence, rdata.ack_sequence, rdata.windowsize, cal_windowsize, rdata.systime,rdata.datalength, rdata.flags, kind, length, wscale);
          insert_data[strlen(insert_data)] = '\0';
          written_to_mysql_winsize += 1;
          // fprintf(fp, "%s\n", insert_data);
          // strncpy(ringbuff6666[write_end6666], insert_data, strlen(insert_data));
          strcpy(ringbuff6666[write_end6666], insert_data);
          write_end6666 += 1;
          write_end6666 = write_end6666 % buffer_size_class1;
          buffer_size6666 += 1;
          if (buffer_size6666 > write_threshold)
            write_to_public_buffer(ringbuff6666, &write_start6666, &buffer_size6666);

        }
        else
          offset = 2 * Win6666;
      }
      pthread_cond_signal(&cond);
      pthread_mutex_unlock(&mutex);
    }
  // fclose(fp);
  free(ptr_uc);
  hash_table_release();
  free(str);
}

static void *receie6026(void *arg)
{
  const int Win6026 = 1024;

  struct sockaddr_in addr;
  int sockfd, len = 0;    
  int addr_len = sizeof(struct sockaddr_in);

  char buffer[Win6026];  
  struct data_dropped rdata;
  int offset = 0;
  unsigned char *ptr_uc;
  char ip_src[20] = { 0 };
  char ip_dst[20] = { 0 };

  char mac_addr[18];
  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
      perror ("socket");
      exit(1);
  }
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(6026);
  addr.sin_addr.s_addr = htonl(INADDR_ANY) ;

  if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr))<0) {
      perror("connect");
      exit(1);
  }
  ptr_uc = malloc(sizeof(__be32));
  while(1) 
    { 
      char insert_data[600];
      
      memset(buffer, 0, sizeof(buffer));
      len = recvfrom(sockfd, buffer, sizeof(buffer), 0, 
                     (struct sockaddr *)&addr ,&addr_len);

      if (len != -1)
        packet6026 += 1;
      offset = 0;
      while((Win6026 > (offset + Reclen6026)))
      {
        int key = 0;
        memcpy(&rdata, buffer+offset, Reclen6026);
        if(rdata.systime !=0)
        {
          offset = offset + Reclen6026;
          reversebytes_uint64t(&rdata.systime);
          reversebytes_uint32t(&rdata.pad1);
          reversebytes_uint32t(&rdata.sequence);
          reversebytes_uint32t(&rdata.ack_sequence);
          reversebytes_uint32t(&rdata.ip_src);
          reversebytes_uint32t(&rdata.ip_dst);
          reversebytes_uint16t(&rdata.port_src);
          reversebytes_uint16t(&rdata.port_dst);
          ptr_uc = (unsigned char *)&rdata.ip_src;
          sprintf(ip_src,"%u.%u.%u.%u", ptr_uc[3], ptr_uc[2], ptr_uc[1], ptr_uc[0]);
          ptr_uc = (unsigned char *)&rdata.ip_dst;
          sprintf(ip_dst,"%u.%u.%u.%u", ptr_uc[3], ptr_uc[2], ptr_uc[1], ptr_uc[0]);
          if (key == 0)
          {
            mac_tranADDR_toString_r(rdata.mac_addr, mac_addr, 18);
            key = 1;
          }

          struct data_dropped_processed *tmp;
          tmp = dropped_buffer[dbend];
          tmp->ip_src = rdata.ip_src;
          tmp->ip_dst = rdata.ip_dst;
          strcpy(tmp->mac_addr, mac_addr);
          tmp->port_src = rdata.port_src;
          tmp->port_dst = rdata.port_dst;
          tmp->drop_location = rdata.pad1;
          tmp->sequence = rdata.sequence;
          tmp->ack_sequence = rdata.ack_sequence;
          tmp->systime = rdata.systime;

          dbamount = dbamount + 1;
          dbamount1 = dbamount1 + 1;
          dbend = dbend + 1;
          dbend = dbend % BUFFER_STORE;
          if(dbamount > BUFFER_STORE)
          {
            dbstart = dbend + 1;
            dbstart = dbstart % BUFFER_STORE;
            dbamount = BUFFER_STORE;
          }
          if(dbamount1 > BUFFER_STORE)
          {
            dbstart1 = dbend + 1;
            dbstart1 = dbstart1 % BUFFER_STORE;
            dbamount1 = BUFFER_STORE;
          }

          memset(insert_data, 0, 600);
          sprintf(insert_data, "INSERT INTO dropped(ip_src, ip_dst, mac_addr, port_src, port_dst, drop_location, sequence, ack_sequence, time)VALUES(\"%s\", \"%s\", \"%.18s\", %u, %u, %u, %u, %u, %llu)\0", ip_src, ip_dst, mac_addr, rdata.port_src, rdata.port_dst, rdata.pad1, rdata.sequence, rdata.ack_sequence, rdata.systime);
          // printf("%s\n", insert_data);
          insert_data[strlen(insert_data)] = '\0';
          written_to_mysql_drop += 1;
          strcpy(ringbuff6026[write_end6026], insert_data);
          buffer_size6026 += 1;
          write_end6026 += 1;
          write_end6026 = write_end6026 % buffer_size_class1;
          if (buffer_size6026 > write_threshold)
            write_to_public_buffer(ringbuff6026, &write_start6026, &buffer_size6026);            
        }
        else
          offset = 2* Win6026;
      }
      usleep(10000);
    }
  free(ptr_uc);

}


static void *insertmysql(void *arg)
{
  while(1)
  {
    int step_commit = 0, res = 0;
    MYSQL my_connection;
    char *ptr = NULL;
    mysql_init(&my_connection);
    if (!mysql_real_connect(&my_connection, "localhost", "root", "ling", "recordudp831", 0, NULL, 0)) 
    {
      printf("connection failed\n");
    }
    pthread_mutex_lock(&mutex_public_buff);
    pthread_cond_wait(&cond_public_buff, &mutex_public_buff);
    mysql_query(&my_connection, "set autocommit = 0");
    while(public_buff_size > 5000)
    {
      ptr = NULL;
      ptr = publicbuff[write_start_public];
      res = mysql_query(&my_connection, ptr);
      if (res)
      {
        fprintf(stderr, "insert error %d: %s\n", mysql_errno(&my_connection), mysql_error(&my_connection));
        printf("ptr is %s write_end_public %d write_start_public %d\n", ptr, write_end_public, write_start_public);
        exit(1);
      }
      publicbuff[write_start_public];      
      write_start_public += 1;
      write_start_public = write_start_public % buffer_size_public;

      public_buff_size -= 1;
      written_to_mysql += 1;
      step_commit += 1;
      step_commit  = step_commit % 1000000;
      if (step_commit > 5000)
      {
        mysql_query(&my_connection, "commit");
        mysql_query(&my_connection, "set autocommit = 1");
        mysql_query(&my_connection, "set autocommit = 0");
      }
    }
    mysql_query(&my_connection, "set autocommit = 1");
    pthread_mutex_unlock(&mutex_public_buff);
    mysql_close(&my_connection);
    usleep(1000);
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
              if ((time - pHead->time) > 240000)
              {
                hash_table_remove(pHead->sKey); 
              } 
              pHead = pHead->pNext;  
          }  
      }
    }    
    pthread_mutex_unlock(&mutex);
    sleep(1);
  }  
}
static void *schedule(void *arg)
{
  struct aplist *neibours;
  char mac_list[20][18];
  int i = 0, amount = 0, j = 0;
  printf("in schedule\n");
  while(1)
  {
    amount = get_aps(mac_list);
    printf("amount is %d\n", amount);
    for(i = 0; i< amount; i++)
    {
      // bool result = 0;
      // struct aplist *neibours;
      // neibours = (struct aplist *)malloc(sizeof(struct aplist));
      // get_clients(mac_list[i], neibours);
      // printf("neibours->length is %d\n", neibours->length);
      // for(j = 0; j < neibours->length; j++)
      // {
      //   if(j > 20)
      //     break;
      //   printf("%s\n", neibours->aplists[j]);
      // }
      // printf("ap %d is %s \n", i, mac_list[i]);
      // struct wireless_information_ap *ap_wireless_info;
      // ap_wireless_info = (struct wireless_information_ap *)malloc(sizeof(struct wireless_information_ap));
      // result = get_wireless_info(mac_list[i], ap_wireless_info);
      // if(result)
      // {
      //   printf("AP %s's noise %d connected_time %llu active_time %llu busy_time %llu receive_time %llu transmit_time %llu\n", mac_list[i], ap_wireless_info->noise, ap_wireless_info->connected_time, ap_wireless_info->active_time, ap_wireless_info->busy_time, ap_wireless_info->receive_time, ap_wireless_info->transmit_time);
      // }
      struct flowlist *flows;
      flows = (struct flowlist *)malloc(sizeof(struct flowlist));
      get_flows(mac_list[i], flows);
      for(j = 0; j < flows->length; j++)
      {
        printf("ip_src %u ip_dst %u port_src %d port_dst %d\n", flows->flows[j].ip_src, flows->flows[j].ip_dst, flows->flows[j].port_src, flows->flows[j].port_dst);
      }
    }
    sleep(2);
  }
  // neibours = get_neighbour();
}
int main()
{
  
  pthread_t tid1, tid2, tid3, tid4, tid5, tid6, tid7;
  int i = 0, err = 0;
  for (i= 0; i< buffer_size_class1; i++)
  {
    ringbuff6666[i] = (char *)malloc(600);
    ringbuff6666[i][1] = '\0';
    ringbuff6683[i] = (char *)malloc(600);
    ringbuff6683[i][1] = '\0';
    ringbuff6681[i] = (char *)malloc(600);
    ringbuff6681[i][1] = '\0';
    ringbuff6682[i] = (char *)malloc(600);
    ringbuff6682[i][1] = '\0';
    ringbuff6026[i] = (char *)malloc(600);
    ringbuff6026[i][1] = '\0';
  }
  for (i = 0; i < buffer_size_public; i++)
    publicbuff[i] = (char *)malloc(600);
  for (i = 0; i < BUFFER_STORE; i++)
  {
    iw_buffer[i] = (struct data_iw_processed *)malloc(sizeof(struct data_iw_processed));
    beacon_buffer[i] = (struct data_beacon_processed *)malloc(sizeof(struct data_beacon_processed));
    queue_buffer[i] = (struct data_queue_processed *)malloc(sizeof(struct data_queue_processed));
    dropped_buffer[i] = (struct data_dropped_processed *)malloc(sizeof(struct data_dropped_processed));
    winsize_buffer[i] = (struct data_winsize_processed *)malloc(sizeof(struct data_winsize_processed));
  }


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
  err = pthread_create(&tid4, NULL, receie6026, NULL);
  if(err != 0)
  {
    printf("receive6026%d creation failed \n", i);
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
  err = pthread_create(&tid7, NULL, schedule, NULL);
  if(err !=  0)
  {
    printf("schedule %d creation failed \n", i);
    exit(1);
  }

  for (i= 0; i< buffer_size_class1; i++)
  {
    free(ringbuff6666[i]);
    free(ringbuff6683[i]);
    free(ringbuff6681[i]);
    free(ringbuff6682[i]);
    free(ringbuff6026[i]);
  }
  for (i = 0; i < buffer_size_public; i++)
    free(publicbuff[i]);

  pthread_exit(0);
  pthread_cond_destroy(&cond);
  pthread_cond_destroy(&cond_public_buff);
  pthread_mutex_destroy(&mutex_public_buff);
  return 0;
}