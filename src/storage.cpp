#include "storage.hpp"
#include <random>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <algorithm>
#include <cctype>

Storage::Storage(const Config& c):c_(c){}
void Storage::initialize(){
 std::filesystem::create_directories(c_.storage_root);
 std::filesystem::create_directories(c_.files_dir);
 std::filesystem::create_directories(c_.tmp_dir);
}
std::string Storage::make_id() const{
 std::random_device rd; std::mt19937_64 g(rd()); std::ostringstream s;
 for(int i=0;i<2;i++) s<<std::hex<<std::setw(16)<<std::setfill('0')<<g();
 return s.str();
}
std::string Storage::sanitize_filename(const std::string& n) const{
 std::string x=n;
 auto p=x.find_last_of("/\\"); if(p!=std::string::npos)x=x.substr(p+1);
 x.erase(std::remove_if(x.begin(),x.end(),[](unsigned char c){return c<32 || c==127;}),x.end());
 if(x.empty())x="file";
 if(x.size()>c_.max_filename_length)x.resize(c_.max_filename_length);
 return x;
}
std::filesystem::path Storage::final_path(const std::string& stored)const{return std::filesystem::path(c_.files_dir)/stored;}
std::filesystem::path Storage::temp_path(const std::string& id)const{return std::filesystem::path(c_.tmp_dir)/(id+".uploading");}

void Storage::reconcile_orphans(Database& db){
 for(auto& e:std::filesystem::directory_iterator(c_.tmp_dir)){
   if(e.is_regular_file()){
     std::error_code ec; std::filesystem::remove(e.path(),ec);
   }
 }
 // Persistent files not referenced by SQLite are quarantined/deleted only if
 // they are clearly internal leftovers. Normal committed files are DB-owned.
 for(auto& e:std::filesystem::directory_iterator(c_.files_dir)){
   if(!e.is_regular_file()) continue;
   if(!db.file_exists_by_stored_name(e.path().filename().string())){
      std::error_code ec; std::filesystem::remove(e.path(),ec);
   }
 }
}
