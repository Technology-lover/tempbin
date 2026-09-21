#include "cleanup.hpp"
#include <chrono>
#include <filesystem>

CleanupWorker::CleanupWorker(const Config& c,Database& d,Storage& s):c_(c),db_(d),storage_(s){}
CleanupWorker::~CleanupWorker(){stop();}
void CleanupWorker::start(){thread_=std::thread(&CleanupWorker::loop,this);}
void CleanupWorker::stop(){stopping_=true;if(thread_.joinable())thread_.join();}
void CleanupWorker::loop(){
 while(!stopping_){
  const auto now=std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  for(auto& r:db_.expired_files(now)){
    auto p=storage_.final_path(r.stored_name);
    std::error_code ec; std::filesystem::remove(p,ec);
    if(!ec || !std::filesystem::exists(p)) db_.delete_file(r.id);
  }
  for(auto& r:db_.expired_pastes(now)) db_.delete_paste(r.id);
  for(unsigned i=0;i<c_.cleanup_interval_seconds && !stopping_;++i)
    std::this_thread::sleep_for(std::chrono::seconds(1));
 }
}
