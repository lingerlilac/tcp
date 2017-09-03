const int Reclen6682 = 40;
struct data_beacon
{
    unsigned char mac_addr[8];
    unsigned char data_rate[4];
    unsigned char current_channel[4];
    unsigned char channel_type[4];
    unsigned char ssl_signal[4];
    __u64 time_current;
    unsigned char bssid[8];
};

struct data_beacon_processed
{
    unsigned char mac_addr[18];
    __u32 data_rate;
    __u32 current_channel;
    unsigned char channel_type[4];
    __be32 ssl_signal;
    __u64 time_current;
    unsigned char bssid[18];
};
const int Reclen6681 = 56;
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
  // __u32 overlimits;
  // __s32 deficit;
  // __u32 ldelay; 
  // __u32 count;
  // __u32 lastcount;
  // __u32 dropping;
  // __s32 drop_next;
  char mac_addr[8];
};
const int Reclen6683 = 112;
struct data_queue_processed {
  __u64 time;
  __u32 queue_id;
  __u64 bytes;
  __u32 packets;
  __u32 qlen;
  __u32 backlog;
  __u32 drops;
  __u32 requeues;
  char mac_addr[8];
};
struct data_iw
{
    __u64 time;
    char station[8];
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
    __s32 noise;
    __u32 expected_throughput;
    __u32 connected_time;
    __u64 active_time;
    __u64 busy_time;
    __u64 receive_time;
    __u64 transmit_time;
};

struct data_iw_processed
{
    __u64 time;
    char station[18];
    char mac_addr[18];
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
    __s32 noise;
    __u32 expected_throughput;
    __u32 connected_time;
    __u64 active_time;
    __u64 busy_time;
    __u64 receive_time;
    __u64 transmit_time;
};
const int Reclen6666 = 64;
struct data_winsize {
  __be32 ip_src;
  __be32 ip_dst;
  __be16 sourceaddr;
  __be16 destination;
  __be32 sequence;
  __be32 ack_sequence;
  __be16 flags;
  __be16 windowsize;
  __be64 systime;
  __be32 datalength;
  char wscale[3];
  char mac_addr[6];
  char eth_src[6];
  char eth_dst[6];
  char pad[7];
};

struct data_winsize_processed {
  __be32 ip_src;
  __be32 ip_dst;
  __be16 sourceaddr;
  __be16 destination;
  __be32 sequence;
  __be32 ack_sequence;
  __be16 flags;
  __be16 windowsize;
  __be64 systime;
  __be32 datalength;
  char mac_addr[18];
  char eth_src[18];
  char eth_dst[18];
  int kind;
  int length;
};
const int Reclen6026 = 40;

struct data_dropped {
  __u32 ip_src;
  __u32 ip_dst;
  char mac_addr[6];
  __u16 pad0;
  __u16 port_src;
  __u16 port_dst;
  __u32 pad1;
  __u32 sequence;
  __u32 ack_sequence;
  __u64 systime;
};
struct data_dropped_processed {
  __u32 ip_src;
  __u32 ip_dst;
  char mac_addr[6];
  __u16 port_src;
  __u16 port_dst;
  __u32 drop_location;
  __u32 sequence;
  __u32 ack_sequence;
  __u64 systime;
};

struct aplist
{
  char aplists[20][18];
  int length;
};

struct flow
{
  __be32 ip_src;
  __be32 ip_dst;
  __u16 port_src;
  __u16 port_dst;
};
struct flowlist
{
  struct flow flows[40];
  int length;
};

struct flow_and_dropped
{
  struct flow dataflow;
  int drops;
};

struct flow_drop_ap
{
  struct flow_and_dropped flowinfo[50];
  int drops_total;
  int length;
};

struct wireless_information_ap
{
  __s32 noise;
  __u32 connected_time;
  __u64 active_time;
  __u64 busy_time;
  __u64 receive_time;
  __u64 transmit_time;
};