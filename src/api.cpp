#include "api.hpp"
#include <fstream>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <nlohmann/json.hpp>

using json=nlohmann::json;
static int64_t now(){return std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());}
static crow::response jres(int code,const json& j){crow::response r(code);r.set_header("Content-Type","application/json");r.body=j.dump();return r;}
static std::string fmt_remaining(int64_t sec){
 if(sec<=0)return "expired";
 int64_t d=sec/86400;sec%=86400;int64_t h=sec/3600;sec%=3600;int64_t m=sec/60;
 std::ostringstream s;if(d)s<<d<<"d ";if(h)s<<h<<"h ";if(m||(!d&&!h))s<<m<<"m";return s.str();
}

void register_routes(crow::SimpleApp& app,const Config& c,Database& db,Storage& storage){
 CROW_ROUTE(app,"/api/health")([&]{return jres(200,{{"status","ok"},{"time",now()}});});

 CROW_ROUTE(app,"/api/pastes").methods(crow::HTTPMethod::GET)([&]{
   auto rows=db.list_pastes(now(),c.max_pastes_per_page);json a=json::array();
   for(auto&r:rows)a.push_back({{"id",r.id},{"preview",r.content.substr(0,std::min<size_t>(140,r.content.size()))},{"created_at",r.created_at},{"expires_at",r.expires_at},{"remaining",fmt_remaining(r.expires_at-now())}});
   return jres(200,{{"items",a}});
 });
 CROW_ROUTE(app,"/api/paste/<string>").methods(crow::HTTPMethod::GET)([&](std::string id){
   PasteRecord r;if(!db.get_paste(id,r)||r.expires_at<=now())return jres(404,{{"error","paste not found"}});
   return jres(200,{{"id",r.id},{"content",r.content},{"created_at",r.created_at},{"expires_at",r.expires_at},{"remaining",fmt_remaining(r.expires_at-now())}});
 });
 CROW_ROUTE(app,"/api/paste").methods(crow::HTTPMethod::POST)([&](const crow::request& req){
   if(req.body.size()>c.max_paste_size)return jres(413,{{"error","paste too large"}});
   if(req.body.empty())return jres(400,{{"error","empty paste"}});
   PasteRecord r;r.id=storage.make_id();r.content=req.body;r.created_at=now();r.expires_at=r.created_at+c.paste_expiry_seconds;
   if(!db.insert_paste(r))return jres(500,{{"error","database insert failed"}});
   return jres(201,{{"id",r.id},{"expires_at",r.expires_at}});
 });
 CROW_ROUTE(app,"/api/paste/<string>").methods(crow::HTTPMethod::DELETE)([&](std::string id){
   return db.delete_paste(id)?jres(200,{{"deleted",true}}):jres(404,{{"error","not found"}});
 });

 CROW_ROUTE(app,"/api/files").methods(crow::HTTPMethod::GET)([&]{
   auto rows=db.list_files(now(),c.max_files_per_page);json a=json::array();
   for(auto&r:rows)a.push_back({{"id",r.id},{"name",r.original_name},{"size",r.size},{"created_at",r.created_at},{"expires_at",r.expires_at},{"remaining",fmt_remaining(r.expires_at-now())},{"url","/file/"+r.stored_name}});
   return jres(200,{{"items",a}});
 });

 CROW_ROUTE(app,"/api/files").methods(crow::HTTPMethod::POST)([&](const crow::request& req){
   if(req.body.size()>c.max_file_size+1024*1024)return jres(413,{{"error","request exceeds configured upload limit"}});
   crow::multipart::message msg(req);
   if(msg.parts.empty())return jres(400,{{"error","multipart upload required"}});
   auto part=msg.parts[0];
   //std::string original=part.get_header_object("Content-Disposition").params["filename"]; this shows errors
   const auto& disposition = part.get_header_object("Content-Disposition");
   auto filename_it = disposition.params.find("filename");

   if (filename_it == disposition.params.end()) {
       return crow::response(400, "Missing filename");
   }

std::string original = filename_it->second;
   original=storage.sanitize_filename(original);
   if(part.body.size()>c.max_file_size)return jres(413,{{"error","file exceeds configured upload limit"}});
   const auto id=storage.make_id();
   const auto tmp=storage.temp_path(id);
   const auto stored=id+"_"+original;
   const auto final=storage.final_path(stored);
   {std::ofstream out(tmp,std::ios::binary|std::ios::trunc);if(!out)return jres(500,{{"error","cannot create temporary file"}});out.write(part.body.data(),(std::streamsize)part.body.size());out.flush();if(!out)return jres(500,{{"error","write failed"}});}
   std::error_code ec;std::filesystem::rename(tmp,final,ec);
   if(ec){std::filesystem::remove(tmp);return jres(500,{{"error","atomic commit failed"}});}
   FileRecord r{ id,original,stored,(uint64_t)part.body.size(),now(),now()+c.file_expiry_seconds };
   if(!db.insert_file(r)){std::filesystem::remove(final);return jres(500,{{"error","database commit failed"}});}
   return jres(201,{{"id",id},{"name",original},{"url","/file/"+stored},{"expires_at",r.expires_at}});
 });

 CROW_ROUTE(app,"/file/<string>").methods(crow::HTTPMethod::GET)([&](std::string stored){
   if(stored.find("..")!=std::string::npos || stored.find('/')!=std::string::npos || stored.find('\\')!=std::string::npos)
      return crow::response(400);
   FileRecord r;if(!db.get_file_by_stored_name(stored,r)||r.expires_at<=now())return crow::response(404);
   auto p=storage.final_path(r.stored_name);std::ifstream in(p,std::ios::binary);if(!in)return crow::response(404);
   std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
   crow::response res(200,body);res.set_header("Content-Type","application/octet-stream");
   res.set_header("Content-Disposition","attachment; filename=\""+r.original_name+"\"");
   res.set_header("X-Expires-At",std::to_string(r.expires_at));return res;
 });
}
