#include "constants.h"


#include <dynamic_reconfigure/server.h>

#include <sensor_msgs/LaserScan.h>

#include <olei_msgs/OleiPacket.h>
#include <olei_msgs/OleiScan.h>
#include <olelidar/OleiPuckConfig.h>

// here maskoff waring of macro 'ROS_LOG'
#pragma clang diagnostic ignored "-Wzero-as-null-pointer-constant"

namespace olelidar {

using namespace sensor_msgs;
using namespace olei_msgs;



class Decoder {
 public:

  explicit Decoder(const ros::NodeHandle& pnh);

  Decoder(const Decoder&) = delete;
  Decoder operator=(const Decoder&) = delete;

  void PacketNormalize(std::vector<uint16_t> &packet_in,std::vector<uint16_t> &packet_out);
  void PacketCb(const OleiPacketConstPtr& packet_msg);
  void ConfigCb(OleiPuckConfig& config, int level);
  void PublishMsg(ros::Publisher* pub,std::vector<uint16_t> &packet_r,std::vector<uint16_t> &packet_i);
  
 private:
  /// 9.3.1.3 Data Point
  /// A data point is a measurement by one laser channel of a relection of a
  /// laser pulse
  struct DataPoint {
    uint16_t azimuth;       //unit: 0.01 degree
    uint16_t distance;      //unit: 1mm 
    uint16_t reflectivity;   //unit: 1 percent,100 is the reflet of white target
    uint16_t distance2;     //rsv for dual return
  } __attribute__((packed));
  static_assert(sizeof(DataPoint) == 8, "sizeof(DataPoint) != 8");

  /// 9.3.1.1 Firing Sequence,followed by velodyne driver,2d=1 
  struct FiringSequence {
    DataPoint points[kFiringsPerSequence];  // 8
  } __attribute__((packed));
  static_assert(sizeof(FiringSequence) == 8, "sizeof(FiringSequence) != 8");

  /// 9.3.1.4 Azimuth,rsv for 3d
  
  /// 9.3.1.5 Data Block,2d format  
  struct DataHead {
        uint8_t magic[4];
        uint8_t version[2];
        uint8_t scale;
        uint8_t oem[3];
        uint8_t model[12];
        uint8_t code[2];
        uint8_t hw[2];
        uint8_t sw[2];
        uint32_t timestamp;
        uint16_t rpm;
        uint8_t flag[2];
        uint8_t rsv[4];
    } __attribute__((packed));
    static_assert(sizeof(DataHead) == 40, "sizeof(DataBlock) != 40");

  
  struct DataBlock {
    FiringSequence sequences[kSequencesPerBlock];  // 8 * 1
  } __attribute__((packed));
  static_assert(sizeof(DataBlock) == 8, "sizeof(DataBlock) != 8");
  
  /// 9.3.1.5 Packet format for 2d
  struct Packet {
    DataHead head;
    DataBlock blocks[kBlocksPerPacket];  // 8 * 150
    /// The four-byte time stamp is a 32-bit unsigned integer marking the moment
    /// of the first data point in the first firing sequcne of the first data
    /// block. The time stamp’s value is the number of microseconds elapsed
    /// since the top of the hour.
    /// uint32_t stamp;
    /// uint8_t factory[2];
  } __attribute__((packed));
  static_assert(sizeof(Packet) == 1240, "sizeof(DataBlock) != 1240");
  static_assert(sizeof(Packet) == sizeof(olei_msgs::OleiPacket().data),"sizeof(Packet) != 1240");

  void DecodeAndFill(const Packet* const packet_buf,uint64_t);

 private:
  bool CheckFactoryBytes(const Packet* const packet);
  void Reset();

  // ROS related parameters
  std::string frame_id_;
  int range_max_;
  int ange_start_;
  int ange_end_;
  ros::NodeHandle pnh_;
  // sub driver topic(msg) 
  ros::Subscriber packet_sub_;
  // pub laserscan message  
  ros::Publisher scan_pub_;

  // dynamic param server
  dynamic_reconfigure::Server<OleiPuckConfig> cfg_server_;
  OleiPuckConfig config_;


  // add vector for laserscan
  std::vector<uint16_t> scanAngleVec_;
  std::vector<uint16_t> scanRangeVec_;
  std::vector<uint16_t> scanIntensityVec_;
  std::vector<uint16_t> scanRangeInVec_;
  std::vector<uint16_t> scanRangeOutVec_;
  std::vector<uint16_t> scanIntensityInVec_;
  std::vector<uint16_t> scanIntensityOutVec_;
  uint16_t azimuthLast_;
  uint16_t azimuthNow_;
  uint16_t azimuthFirst_;

  // laserscan msg 
  uint32_t scanMsgSeq_;
  
};

Decoder::Decoder(const ros::NodeHandle& pnh)
    : pnh_(pnh), cfg_server_(pnh) {
  // get param from cfg file at node start     
  pnh_.param<std::string>("frame_id", frame_id_, "olelidar");
  pnh_.param<int>("r_max", range_max_, 30);
  ROS_INFO("===========================");
  ROS_INFO("Ole Frame_id: %s", frame_id_.c_str());
  ROS_INFO("Ole Topic: %s/scan", frame_id_.c_str());
  ROS_INFO("Ole RangeMax: %d mm", range_max_);
  ROS_INFO("===========================");
  // dynamic callback 
  cfg_server_.setCallback(boost::bind(&Decoder::ConfigCb, this, _1, _2));
  // packet receive
  azimuthLast_=0;
  azimuthNow_=0;
  azimuthFirst_=0xFFFF;
  // laserscan msg init
  scanMsgSeq_ = 0;
}


void Decoder::PublishMsg(ros::Publisher* pub,std::vector<uint16_t> &packet_r,std::vector<uint16_t> &packet_i)
{
  sensor_msgs::LaserScan scanMsg;
  uint16_t route = uint16_t(config_.route);
  //ROS_INFO("route:%d", route);
  /*
  std_msgs/Header header
        uint32 seq
        time stamp
        string frame_id
  float32 angle_min
  float32 angle_max
  float32 angle_increment
  float32 time_increment
  float32 scan_time
  float32 range_min
  float32 range_max
  float32[] ranges
  float32[] intensities
  */
  
  //float siz= scanRangeVec_.size();

  //route=siz;
  //config_.step=(360.0*1.0f)/(route * 1.0f);

  //ROS_INFO("step:%f    route:%f", 1.1, siz);

  //scanMsg.header.stamp = start;
  scanMsg.header.seq = scanMsgSeq_;
  scanMsgSeq_++;
  // maybe get from head??
  scanMsg.header.stamp = ros::Time::now();
  scanMsg.header.frame_id = frame_id_.c_str();
 
  // below constant future from cfg param 

  scanMsg.angle_min = deg2rad(config_.angle_min);
  scanMsg.angle_max = deg2rad(config_.angle_max);
  //scanMsg.scan_time = scan_time;
  scanMsg.scan_time = 1.0f/config_.freq;
  
  scanMsg.angle_increment = deg2rad(config_.step);
  scanMsg.time_increment = scanMsg.scan_time / route ;

  scanMsg.range_min = config_.range_min;  //unit:m
  scanMsg.range_max = config_.range_max;

  //int effective_count = 1600;
  //std::reverse(myvector.begin(),myvector.end());
  //scanMsg.ranges = std::reverse(scanRangeVec_.begin(),scanRangeVec_.end());
  double min=scanMsg.angle_min;
  double max=scanMsg.angle_max;
 // ROS_INFO("Min:%f  Max:%f", min, max);


  scanMsg.ranges.resize(route);
  scanMsg.intensities.resize(route);

  //ROS_INFO("start to build ranges and intensities");
  int t=0;
  for(uint16_t i=0;i<route;i++){
    // reverse,laserscan is anticlockwise,
    scanMsg.ranges[i] = scanRangeOutVec_[route-1 - i] * 0.001f;
    scanMsg.intensities[i] = scanIntensityOutVec_[route-1 - i] * 1.0f;
    // mask err,
    if(scanMsg.ranges[i] > scanMsg.range_max)
      scanMsg.ranges[i] = scanMsg.range_max;    
    t++;
  }

  if(scanAngleVec_[0]<22.5) //当起始角度从0开始才被认为合法帧
  {
    //uint16_t len=scanAngleVec_[0];
    //ROS_INFO("Length:%d",len);
     pub->publish(scanMsg);
  }

}



void Decoder::DecodeAndFill(const Packet* const packet_buf, uint64_t time) {
  
  // For each data block, 150 total
  uint16_t azimuth;
  uint16_t range;
  uint16_t intensity;

  azimuthNow_=packet_buf->blocks[0].sequences[0].points[0].azimuth;

  if(azimuthFirst_==0xFFFF) azimuthFirst_=azimuthNow_;
  //ROS_INFO("azimuthNow:%d",azimuthNow_);
  //folled velodyne,comp of 3d 
  for (int iblk = 0; iblk < kBlocksPerPacket; ++iblk) {
    const auto& block = packet_buf->blocks[iblk];

    // simple loop
    azimuth =block.sequences[0].points[0].azimuth;
    range = block.sequences[0].points[0].distance;
    intensity = block.sequences[0].points[0].reflectivity;
    //ROS_INFO("intensity:%d",intensity);
    if(range>range_max_) {range=0;intensity=0;}
    // azimuth ge 36000 is not valid
    if(azimuth < 0xFF00){
      scanAngleVec_.push_back(azimuth);
      scanRangeVec_.push_back(range);
      scanIntensityVec_.push_back(intensity);
    }
  }


}


void Decoder::PacketNormalize(std::vector<uint16_t> &packet_in,std::vector<uint16_t> &packet_out) {

  uint16_t size_in = uint16_t(packet_in.size());
  uint16_t size_std = uint16_t(config_.route);
  float div = (size_in-1)/(size_std -1);
  uint16_t size_std_dec = size_std -1;
  uint16_t size_in_dec = size_in -1;

  float index,index_frac;
  uint16_t index_int;
  float diff;
  float val;


  packet_out.clear();
  packet_out.push_back(packet_in[0]);

  for(uint16_t i=1;i<size_std_dec;i++) {

    index = div * i;
    index_int = uint16_t(index);
    index_frac = index - index_int;
    diff = packet_in[index_int+1] - packet_in[index_int];
    val = packet_in[index_int] + diff * index_frac;
    packet_out.push_back(uint16_t(val));

  }

  packet_out.push_back(packet_in[size_in_dec]);

}



void Decoder::PacketCb(const OleiPacketConstPtr& packet_msg) {
  const auto start = ros::Time::now();
  // uint16_t route = uint16_t(config_.route);

  const auto* packet_buf =
      reinterpret_cast<const Packet*>(&(packet_msg->data[0]));

  //DataHead packet_head =packet_buf ->head;
  // later use packet_head.timestamp
  azimuthNow_=packet_buf->blocks[0].sequences[0].points[0].azimuth;

  //ROS_INFO("azimuthLast_:%d now:%d", azimuthLast_,azimuthNow_);
  // below constant furture replaced with cfg param
  if(azimuthLast_ < azimuthNow_){
    DecodeAndFill(packet_buf, packet_msg->stamp.toNSec());
    azimuthLast_ =  azimuthNow_;
    return;
  }
 else
 {
  azimuthLast_ =  azimuthNow_;
 }

  // scan first half route
  if(azimuthFirst_ >200){
    azimuthFirst_ = azimuthNow_;
    return;
  }
//ROS_INFO("==============================");
   config_.route =scanRangeVec_.size();
   config_.step =(360.0*1.0f)/(config_.route*1.0f);
   //  ROS_INFO("route:%d  step:%f",config_.route,config_.step);

  /*
  if((scanRangeVec_.size()!=config_.route) || (scanIntensityVec_.size()!=config_.route)){
    scanRangeVec_.clear();
    scanIntensityVec_.clear();
    return;
  }
   */
  // frame is over,call publish msg active

  //memcpy(rangeBuf, &scanRangeVec_[0], route*sizeof(uint16_t));
  //memcpy(intensityBuf, &scanIntensityVec_[0], route*sizeof(uint16_t));
  scanRangeInVec_.clear();
  scanIntensityInVec_.clear();
  scanRangeOutVec_.clear();
  scanIntensityOutVec_.clear();

  //scanAngleVec_.assign(scanAngleVec_.begin(), scanAngleVec_.end());
  scanRangeInVec_.assign(scanRangeVec_.begin(), scanRangeVec_.end());
  scanIntensityInVec_.assign(scanIntensityVec_.begin(), scanIntensityVec_.end());

  scanAngleVec_.clear();
  scanRangeVec_.clear();
  scanIntensityVec_.clear();

  //PacketNormalize(scanAngleVec_,scanAngleVec_);
  PacketNormalize(scanRangeInVec_,scanRangeOutVec_);
  PacketNormalize(scanIntensityInVec_,scanIntensityOutVec_);


  PublishMsg(&scan_pub_,scanRangeOutVec_,scanIntensityOutVec_);


  DecodeAndFill(packet_buf, packet_msg->stamp.toNSec());
  // Don't forget to reset
  // Reset();
  ROS_DEBUG("Time: %f", (ros::Time::now() - start).toSec());
}

void Decoder::ConfigCb(OleiPuckConfig& config, int level) {
  // config.min_range = std::min(config.min_range, config.max_range);
       //config.route =4000;
pnh_.param<int>("ang_start", ange_start_, 0);
pnh_.param<int>("ang_end", ange_end_, 360);
config.angle_min=ange_start_;
config.angle_max=ange_end_;
      //config.step =360/config.route;
  ROS_INFO(
      "Config:freq: %f, route: %d, step: %f,angle_start: %f, angle_end: %f,range_min: %f, range_max: %f" ,
      config.freq,
      config.route,
      config.step,
      config.angle_min,
      config.angle_max,
      config.range_min,
      config.range_max
      );
  config_ = config;
  Reset();

  if (level < 0) {
    // topic = sacn,msg = LaserScan,queuesize=10
    scan_pub_ = pnh_.advertise<LaserScan>("scan", 10);
    packet_sub_ = pnh_.subscribe<OleiPacket>("packet", 256, &Decoder::PacketCb, this);
    ROS_INFO("Drive Ver:2.0.5");
    ROS_INFO("Decoder initialized");
  }
}

void Decoder::Reset() {

}


}  // namespace olei_puck

int main(int argc, char** argv) {
  ros::init(argc, argv, "olelidar_decoder");
  ros::NodeHandle pnh("~");

  olelidar::Decoder node(pnh);
  ros::spin();
}
