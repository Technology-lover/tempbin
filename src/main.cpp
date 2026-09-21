#include "config.hpp"
#include "database.hpp"
#include "storage.hpp"
#include "cleanup.hpp"
#include "api.hpp"
#include <crow.h>
#include <iostream>
#include <filesystem>

int main(int argc,char**argv){
 try{
  const std::string cfg=(argc>1?argv[1]:"/etc/tempdrop/config.json");
  Config c=Config::load(cfg);
  Storage storage(c);storage.initialize();
  Database db(c.database);db.initialize();
  storage.reconcile_orphans(db);

  crow::SimpleApp app;
  register_routes(app,c,db,storage);
  CleanupWorker cleanup(c,db,storage);cleanup.start();

  std::cout<<"TempDrop listening on "<<c.bind_address<<":"<<c.port
           <<" with "<<c.threads<<" threads\n";
  app.bindaddr(c.bind_address).port(c.port).multithreaded().concurrency(c.threads).run();
  cleanup.stop();
 }catch(const std::exception&e){std::cerr<<"Fatal: "<<e.what()<<"\n";return 1;}
 return 0;
}
