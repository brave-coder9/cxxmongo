#include <iostream>
#include <fstream>
#include <string>
#include <atomic>
#include <thread>
#include <memory>
#include <vector>
#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/json.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/asio.hpp>
#include <boost/thread/thread.hpp>
#include <boost/regex.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/program_options.hpp>

#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/pool.hpp>
#include <mongocxx/exception/bulk_write_exception.hpp>
#include <curl/curl.h>
//#include <tidy/tidy.h>
//#include <tidy/buffio.h>


using namespace std;
namespace po = boost::program_options;

struct Station {
    string url, type, now;
    bsoncxx::types::b_oid id;
    int icy;
};

struct SongDetail {
    bool found;
    bsoncxx::types::b_oid song_id;
    bsoncxx::types::b_oid release_id;
    string release;
    string artist;
};


size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp);
pair<string,string> curl_get (const char *url);
pair<string,string> curl_get (shared_ptr<Station>);
std::string CleanHTML(const std::string &html);
void zone(void);
void check_station (shared_ptr<Station>);
void send_station_to_server(shared_ptr<Station> ptr);
void update_station (shared_ptr<bsoncxx::builder::stream::document>, shared_ptr<bsoncxx::builder::stream::document>);
bool parse_station (shared_ptr<bsoncxx::builder::stream::document> update, pair<string,string> response, shared_ptr<Station>);
bool detect_station (shared_ptr<bsoncxx::builder::stream::document> update, pair<string,string> response, shared_ptr<Station>);
void check_online(void);
void check_all(void);
void ids(void);
string regex_fetch(string in, string rstr);
bsoncxx::types::b_oid match_song (string name);
void fix_db(void);
bsoncxx::types::b_oid match_artist (string name, bool *found = 0);
SongDetail* match_song_ex (string name);

using bsoncxx::builder::stream::open_document;
using bsoncxx::builder::stream::close_document;

// 0    4      8      9     309   609    1017   1018     1022   1023  1024
// 32b  32b    8b     300B  300B  408B   8b     32b      8b     8b
// id   check  karma  url   fURL  title  fail#  lastSuc  error  type
//

const unsigned int NUMTHREADS = 768;
const unsigned int NUMTHREADS_MONGO = 32;
bool DEBUG = false;
atomic<int> stations_checked;
atomic<int> stations_updated;
atomic<int> updates_pending;
map<string, shared_ptr<Station>> station_map;
vector<shared_ptr<Station>> station_vector;
mongocxx::instance inst{};
/* when this app runs on server */
// const string connection_string = "mongodb://mongoadminrole:susuSUSU1234!%40%23%24@127.0.0.1:27018/radio?authSource=admin";
/* when this app runs on my local */
const string connection_string = "mongodb://mongoadminrole:susuSUSU1234!%40%23%24@localhost:27018?authSource=admin&ext.ssh.server=178.33.122.217:22&ext.ssh.username=root&ext.ssh.password=susuSUSU1234!@#$";

mongocxx::pool mongopool {mongocxx::uri{connection_string}};
// mongocxx::pool mongopool {mongocxx::uri{}};
boost::asio::io_service svc_mongo;
boost::asio::io_service svc;
int main (int ac, char **av) {
    stations_checked = stations_updated = updates_pending = 0;

po::options_description desc("Allowed options");
desc.add_options()
    ("help", "produce help message")
    ("online", "parse all online radios")
    ("all", "parse all radios")
    ("url", po::value<string>(), "parse url")
    ("song", po::value<string>(), "match song")
    ("fixdb", "remove bad links and duplicated links")
    ("ids", "remove bad links and duplicated links")
;

po::variables_map vm;
po::store(po::parse_command_line(ac, av, desc), vm);
po::notify(vm);

if (vm.count("help")) {
    cout << desc << "\n";
    return 1;
}

    if (vm.count("song")) {
        DEBUG=true;
        string song = vm["song"].as<string>();
        cout << "Matching song: " << song << endl;
        auto oid = match_song(song);
        cout << "Mongo ID: " << oid.value.to_string() << endl;
        auto entry = mongopool.acquire();
        auto db = (*entry)["discogs"]["songs"];
        auto cursor = db.find_one( bsoncxx::builder::stream::document{} << "_id" << oid.value
            << bsoncxx::builder::stream::finalize);
        if (cursor) cout << "JSON: " << bsoncxx::to_json((*cursor).view()) << endl;

        return 1;
    }


    // std::thread report([](){
    //     unsigned int c=0, u=0;
    //     while (1) {
    //         cout << "Checked: " << stations_checked << " (" << stations_checked-c << "/s) Updated: " << stations_updated
    //             << " (" << stations_updated-u << "/s)" << endl;
    //         cout << updates_pending << " updates pending..." << endl;
    //         c = stations_checked;
    //         u = stations_updated;
    //         sleep(1);
    //     }
    // });

    boost::asio::io_service::work work(svc);
    boost::thread_group threads, threads_mongo;
    boost::asio::io_service::work work_mongo(svc_mongo);
    for (int i = 0; i < NUMTHREADS; ++i)
        threads.create_thread (boost::bind (&boost::asio::io_service::run, &svc));
    for (int i = 0; i < NUMTHREADS_MONGO; ++i)
        threads_mongo.create_thread (boost::bind (&boost::asio::io_service::run, &svc_mongo));

    if (vm.count("fixdb")) {
        fix_db();

        return 1;
    }

    if (vm.count("url")) {
        DEBUG=true;
        shared_ptr<Station> ptr = make_shared<Station>();
        ptr->url = vm["url"].as<string>();
        cout << "Checking URL: " << ptr->url << endl;
        check_station(ptr);
    }

    try {
        if (vm.count("online"))	check_online();
        if (vm.count("all"))	check_all();
        if (vm.count("ids"))	ids();
    } catch (std::exception &e) {
        cout << "***** Exception on main: " << e.what() << endl;
    }


    threads.join_all();
    threads_mongo.join_all();

    return 0;

    /*
    shared_ptr<Station> ptr = make_shared<Station>();
    ptr->url = "http://108.163.221.102/bopxmas";
    ptr->type = "icecast";
    ptr->icy = 16000;
    auto par = curl_get(ptr);
    cout << "Cabecalho:" << endl << endl << par.first << endl;
    // cout << "Corpo:" << endl << endl << par.second << endl;

    auto update = make_shared<bsoncxx::builder::stream::document>();
    parse_station(update, par, ptr);
    cout << "Update: " << bsoncxx::to_json(update->view()) << endl;

    return 0;*/
}


void ids(void) {
    auto entry = mongopool.acquire();
    auto db_releases = (*entry)["discogs"]["releases"];
    auto db_songs = (*entry)["discogs"]["songs"];
    auto cursor = db_releases.find(
    bsoncxx::builder::stream::document{} << "id" << 1
            //<< "type" << "icecast"
            //            << "type" << bsoncxx::builder::stream::open_document << "$exists" << false << bsoncxx::builder::stream::close_document
            << bsoncxx::builder::stream::finalize
            );
    unsigned int c=0;

    cout << "Fetching Mongo data" << endl;
    for (auto doc : cursor) {
        // for (auto song : doc["tracklist"]) {}

//        station_vector.push_back(tmp);
//        cout << "Data: " << endl << bsoncxx::to_json(*ptr) << endl;
//        c++;
//        if (c>2) break;
//        station_map[doc["_id"].get_oid().value.to_string()] = doc["url"].get_utf8().value.to_string();
    }
    std::cout << "Fetched" << '\n';
    /*cout << "Fetched " << station_map.size() << " stations from mongo" << endl;
    cout << "Starting workers" << endl;
    for (auto &i : station_map) {
//        cout << "Data: " << endl << bsoncxx::to_json(*i) << endl;
        svc.post(std::bind(check_station, i.second));
    }
    svc.post(std::bind(check_online));*/
}


void check_online(void) {
    auto entry = mongopool.acquire();
    auto db = (*entry)["radio"]["stations_copy"];
    auto cursor = db.find( bsoncxx::builder::stream::document{}
            // << "response" << true
            // << "station_type" << "icecast"
            // << "station_type" << "shoutcast"
            // << "type" << bsoncxx::builder::stream::open_document << "$exists" << false << bsoncxx::builder::stream::close_document
            << bsoncxx::builder::stream::finalize
            );

    if (DEBUG)
        cout << "Fetching Mongo data" << endl;
    unsigned int c=0;

    for (auto doc : cursor) {
        try {
            //auto ptr = make_shared<bsoncxx::document::view>(doc.data(), doc.length());
            shared_ptr<Station> one_station = make_shared<Station>();
            string str_stream_url = doc["stream"].get_utf8().value.to_string();
            if (DEBUG)
                cout << "-------- stream : " << str_stream_url << endl;

            (*one_station).url = str_stream_url;
            (*one_station).type = doc["station_type"].get_utf8().value.to_string(); // shoutcast, icecast, etc
            (*one_station).id = doc["_id"].get_oid();
            if (doc["metaint"].length() > 0)
                (*one_station).icy = doc["metaint"].get_int32().value;
            station_map[one_station->id.value.to_string()] = one_station;
        } catch (std::exception &e) {
            if (DEBUG)
                cout << "***** Exception on fetching mongo data: " << e.what() << endl;
        }
    
        //zzz+ for test
        // c++;
        // if (c > 2) break;
        //zzz-
    }
    if (DEBUG) {
        cout << "Fetched " << station_map.size() << " stations from mongo" << endl << endl;
        cout << "Starting workers --> " << endl;
    }
    for (auto &i : station_map) {
        svc.post(std::bind(check_station, i.second));
    }
    //zzz+ must uncomment
    // svc.post(std::bind(check_online));
    //zzz-
}



void check_all(void) {
    auto entry = mongopool.acquire();
    auto db = (*entry)["radio"]["stations_copy"];
    auto cursor = db.find({});

    for (auto doc : cursor) {
        //auto ptr = make_shared<bsoncxx::document::view>(doc.data(), doc.length());
        //if (doc["station_type"] == NULL){

          shared_ptr<Station> one_station = make_shared<Station>();
          string strTmp;
          if (doc["stream"])
            strTmp = doc["stream"].get_utf8().value.to_string();
          else
            strTmp = "";
          (*one_station).url = strTmp;
          (*one_station).id = doc["_id"].get_oid();
          if (doc["station_type"]) (*one_station).type = doc["station_type"].get_utf8().value.to_string();
          if (doc["metaint"]) (*one_station).icy = boost::lexical_cast<unsigned int>(doc["metaint"].get_utf8().value.to_string());
          station_map[one_station->id.value.to_string()] = one_station;
        //}
//        station_vector.push_back(one_station);
//        cout << "Data: " << endl << bsoncxx::to_json(*ptr) << endl;
//        c++;
//        if (c>2) break;
//        station_map[doc["_id"].get_oid().value.to_string()] = doc["url"].get_utf8().value.to_string();
    }
    cout << "Fetched " << station_map.size() << " stations from mongo" << endl;
    cout << "Starting workers" << endl;
    for (auto &i : station_map) {
//        cout << "Data: " << endl << bsoncxx::to_json(*i) << endl;
        svc.post(std::bind(check_station, i.second));
    }
    //svc.post(std::bind(check_online));
}

void check_station (shared_ptr<Station> ptr) {
    try {
        // checking the URL of the station
        if (ptr->url.empty()) {
            if (DEBUG) cout << "Check Station URL not found!" << endl;
            return;
        } else {
            if (DEBUG) cout << "Station URL: " << ptr->url << endl;
        }

        // fetching...
        auto resp = curl_get(ptr);

        // processing response...
        string body = resp.second;
        ++stations_checked;
        if (DEBUG) {
            cout << "~~~~~~~~~~~~~~~~ check station ~~~~~~~~~~~~~" << endl;
            cout << "[" << stations_checked <<  "] URL: " << ptr->url << endl;
            cout << "  Header: " << resp.first << endl;
            cout << "  Body: " << !body.empty() << endl;
            // cout << " Body data: " << body << endl;
            cout << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~" << endl;
        }

        auto filter = make_shared<bsoncxx::builder::stream::document>();
        auto update = make_shared<bsoncxx::builder::stream::document>();
        (*filter) << "_id" << ptr->id;
        (*update) << "$set" << open_document; // << "response" << !body.empty();

        bool could_parse = false;
        if (resp.first.empty() && resp.second.empty()) {
            (*update) << "response" << false;
            could_parse = true;
        } else {
            // parsing the body...
            could_parse = parse_station (update, resp, ptr);
        }
        (*update) << close_document;
        if (DEBUG) {
            cout << "Check Station update:" << endl << bsoncxx::to_json(*update) << endl;
        }
        if (could_parse) {
            // svc_mongo.post(std::bind(update_station, filter, update));
            if (ptr->now.empty() == false)
                svc_mongo.post(std::bind(send_station_to_server, ptr));
            // updates_pending++;
        }
        //cout << "Filter: " << bsoncxx::to_json(filter) << endl;
        //cout << "Update: " << bsoncxx::to_json(update) << endl;
        //if (++c >= 1) return 0;
    } catch (std::exception &e) {
        cout << "***** Exception on check_station: " << e.what() << endl;
    }
}

bool parse_station (shared_ptr<bsoncxx::builder::stream::document> update, pair<string,string> response, shared_ptr<Station> ptr) {
    // detect the station type...
    if (ptr->type.empty() || ptr->type=="unknown") {
        detect_station(update, response, ptr);
    }

    cout << "======== Found [" << ptr->type << "] Server: " << ptr->url << endl;
    if (DEBUG) {
        cout << "----- Headers: " << endl << response.first << endl;
        cout << "-----" << endl;
    }
    
    // when the station type is icecast...
    if (ptr->type=="icecast") {
        string metaint = regex_fetch(response.first, "icy-metaint:(\\d*)");
        (*update) << "response" << true;
        (*update) << "station_type" << "icecast";
        if (!metaint.empty()) {
            int pos = boost::lexical_cast<int>(metaint);
            (*update) << "metaint" << pos;
            if (pos < response.second.size()) {
                size_t len = response.second.at(pos);
                len *= 16;
                if (response.second.size() > pos+len+1) {
                    const char* rptr = response.second.c_str();
                    string now_buffer = string(rptr+pos, rptr+pos+len);
                    // cout << "@@@@@@@@ icecast: " << ptr->url << "@@@@ POS: " << pos << " LEN: " << len << " NOW: " << now_buffer << endl;
                    string now_tmp = regex_fetch(now_buffer, "StreamTitle='(.*)';");
                    cout << "now_tmp = " << now_tmp << endl;
                    string now = now_tmp;
                    size_t last_pos = now_tmp.find("';");
                    if (last_pos != string::npos) {
                        now = now_tmp.substr(0, last_pos);
                    }
                    if (!now.empty()) {
                        cout << "@@@@@@@@ icecast: " << ptr->url << " @@@@ Now playing: " << now << " @@@@" << endl;
                        (*update) << "now" << now;
                        ptr->now = now;
                    }
                }
            }
        }
        return true;
    }

    // when the station type is shoutcast...
    if (ptr->type=="shoutcast") {
        (*update) << "response" << true;
        (*update) << "station_type" << "shoutcast";
        string now = regex_fetch(response.second, "<tr><td width=100 nowrap><font class=default>Current Song: </font></td><td><font class=default><b>([^<]*)</b></td>");
        if (!now.empty()){
            cout << "@@@@@@@@ shoutcast: " << ptr->url << " @@@@ Now playing: " << now << " @@@@" << endl;
            (*update) << "now" << now;
            ptr->now = now;
        }
        return true;
    }
    
    // when the station type is other...
    if (ptr->type=="404") {
        (*update) << "response" << false;
        return true;
    }
    if (ptr->type=="private") {
        (*update) << "response" << false;
        (*update) << "station_type" << "private";
        return true;
    }
    if (ptr->type=="shoutcast_dnas") {
        (*update) << "response" << true;
        (*update) << "station_type" << "shoutcast_dnas";
        return true;
    }
    if (ptr->type=="icecast_ms") {
        (*update) << "response" << true;
        (*update) << "station_type" << "icecast_ms";
        return true;
    }
    if (ptr->type=="icecast_broken") {
        (*update) << "response" << true;
        (*update) << "station_type" << "icecast_broken";
        return true;
    }
    if (ptr->type=="wmx") {
        (*update) << "response" << true;
        (*update) << "station_type" << "wmx";
        std::string::const_iterator start, end;
        start = response.second.begin();
        end = response.second.end();
        boost::regex r("^.*<Ref Href = \\\"([^\\\"]*)\\\" />.*");
        boost::match_results<std::string::const_iterator> sm;
        boost::match_flag_type flags = boost::match_default;
        if (regex_search(start, end, sm, r, flags))  {
            if (DEBUG) cout << "Regex match: " << sm[1] <<  endl;
            (*update) << "url_wmx" << sm[1].str();
        }
        return true;
    }
    if (ptr->type=="mpegurl") {
        (*update) << "response" << true;
        (*update) << "station_type" << "mpegurl";
        std::string::const_iterator start, end;
        start = response.second.begin();
        end = response.second.end();
        boost::regex r("^(http://.*)$");
        boost::match_results<std::string::const_iterator> sm;
        boost::match_flag_type flags = boost::match_default;
        if (regex_search(start, end, sm, r, flags))  {
            if (DEBUG) cout << "Regex match: " << sm[1] <<  endl;
            (*update) << "url_mpegurl" << sm[1].str();
        }
        return true;
    }
    if (ptr->type=="pls") {
        (*update) << "response" << true;
        (*update) << "station_type" << "pls";
        std::string::const_iterator start, end;
        start = response.second.begin();
        end = response.second.end();
        boost::regex r("^File\\d*=(http://.*)$");
        boost::match_results<std::string::const_iterator> sm;
        boost::match_flag_type flags = boost::match_default;
        if (regex_search(start, end, sm, r, flags))  {
            if (DEBUG) cout << "Regex match: " << sm[1] <<  endl;
            (*update) << "url_pls" << sm[1].str();
        }
        return true;
    }
    if (ptr->type=="m3u8") {
        (*update) << "response" << true;
        (*update) << "station_type" << "m3u8";
        return true;
    }
    if (ptr->type=="mpeg") {
        (*update) << "response" << true;
        (*update) << "station_type" << "mpeg";
        return true;
    }

    return false;
}

bool detect_station (shared_ptr<bsoncxx::builder::stream::document> update, pair<string,string> response, shared_ptr<Station> ptr) {
    if (response.first.find("ice-audio-info: ") != string::npos ||
        response.first.find("icy-pub:1") != string::npos ||
        response.first.find("Server: Icecast") != string::npos) {
        ptr->type = "icecast";
        return true;
    }
    if (response.second.find("http://www.shoutcast.com/disclaimer.phtml") != string::npos ||
        response.second.find("<title>SHOUTcast Server</title>") != string::npos
       ) {
        ptr->type = "shoutcast";
        return true;
    }
    if (response.first.find("HTTP/1.1 404") != string::npos ||
        response.first.find("HTTP/1.0 404") != string::npos) {
        ptr->type = "404";
        return true;
    }
    if (response.first.find("HTTP/1.0 401") != string::npos ||
        response.second.find("ICY 401") != string::npos ||
        response.second.find("invalid password") != string::npos ||
        response.second.find("Invalid password") != string::npos) {
        ptr->type = "private";
        return true;
    }
    if (response.second.find("<title>SHOUTcast DNAS Summary</title>") != string::npos) {
        ptr->type = "shoutcast_dnas";
        return true;
    }
    if (response.second.find("<title>Icecast Streaming Media Server</title>") != string::npos) {
        ptr->type = "icecast_ms";
        return true;
    }
    if (response.first.empty() && response.second.find("ICY 200 OK") != string::npos) {
        ptr->type = "icecast_broken";
        return true;
    }
    if (response.first.find("Content-Type: video/x-ms-wmx") != string::npos) {
        ptr->type = "wmx";
        return true;
    }
    if (response.first.find("Content-Type: audio/x-mpegurl") != string::npos) {
        ptr->type = "mpegurl";
        return true;
    }
    if (response.first.find("Content-Type: audio/x-scpls") != string::npos) {
        ptr->type = "pls";
        return true;
    }
    if (response.first.find("Content-Type: application/vnd.apple.mpegurl") != string::npos) {
        ptr->type = "m3u8";
        return true;
    }
    if (response.first.find("Content-Type: audio/mpeg") != string::npos) {
        ptr->type = "mpeg";
        return true;
    }

    return false;
}


string regex_fetch(string in, string rstr) {
    std::string::const_iterator start, end;
    start = in.begin();
    end = in.end();
    boost::regex r(rstr);
    boost::match_results<std::string::const_iterator> sm;
    boost::match_flag_type flags = boost::match_default;
    if (regex_search(start, end, sm, r, flags))  {
        return sm[1].str();
    }
    return "";
}

bool regex_match_ex(string src, string rstr)
{
     boost::regex r(rstr);

     return regex_match(src, r);

    //return true;
}

/**
 * No need update the mongodb
 * Therefore I commented the updating codes
 * And then I send it to the nodejs-server.
 */
void send_station_to_server(shared_ptr<Station> ptr)
{
    const string str_nop = "NA";
    string station_type = ptr->type;
    string now = ptr->now, now_id = str_nop;
    string artist = str_nop, artist_id = str_nop;
    string release = str_nop, release_id = str_nop;

    SongDetail* tmp = match_song_ex(now);

    if (tmp->found) {
        now_id = tmp->song_id.value.to_string();
        release = tmp->release;
        release_id = tmp->release_id.value.to_string();
    }
    artist = tmp->artist;
    bool found = false;
    artist_id = match_artist(artist, &found).value.to_string();
    if (found == false)
        artist_id = str_nop;

    delete tmp;
    cout << "$$$$$$$$$$$$ station value $$$$$$$$$$$$" << endl;
    cout << "station_type = " << station_type << endl;
    cout << "now = " << now << endl;
    cout << "now_id = " << now_id << endl;
    cout << "artist = " << artist << endl;
    cout << "artist_id = " << artist_id << endl;
    cout << "release = " << release << endl;
    cout << "release_id = " << release_id << endl;
    cout << "$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$" << endl;
    
    //
    // send to nodejs server
    //
    CURL *curl;
    CURLcode res;
    curl = curl_easy_init();
    if(curl) 
    {
        string data = "";
        data.append("station_type=");
        data.append(station_type);
        data.append("&now=");
        data.append(now);
        data.append("&now_id=");
        data.append(now_id);
        data.append("&artist=");
        data.append(artist);
        data.append("&artist_id=");
        data.append(artist_id);
        data.append("&release=");
        data.append(release);
        data.append("&release_id=");
        data.append(release_id);
        cout << "POSTFIELDS : " << data << endl;

        curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:3000/station_info");
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, data.length());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5);

        res = curl_easy_perform(curl);
        if(CURLE_OK != res)
        {
            printf("Error: %s\n", strerror(res));
        }
        curl_easy_cleanup(curl);
    }
}

void update_station (shared_ptr<bsoncxx::builder::stream::document> filter, shared_ptr<bsoncxx::builder::stream::document> update) {
    stations_updated++;
    updates_pending--;
    auto uview = update->view();
    {
        try {
            auto entry = mongopool.acquire();
            auto db2 = (*entry)["radio"]["stations_copy"];
            db2.update_one(filter->view(), uview);
        } catch (std::exception &e) {
            //cout << "Update Station exception before match: " << e.what() << endl;
            //cout << "Update: " << endl << bsoncxx::to_json(uview) << endl;
            sleep(5);
            return;
        }
    }
    //db.update_one(filter->view(), update->view());
    auto e = uview["$set"]["now"];
    if (e) {
        string now = e.get_utf8().value.to_string();
        if (!now.empty()) {
            if (DEBUG)            cout << "Looking for song: " << now << endl;
            //auto oid = match_song(now);
            SongDetail* tmp = match_song_ex (now);

            if (DEBUG)            cout << "Song ID: " << (*tmp).song_id.value.to_string() << endl;

            auto update2 = make_shared<bsoncxx::builder::stream::document>();
            (*update2) << "$set" << open_document; // << "response" << !body.empty();
            (*update2) << "now_id" << (*tmp).song_id;
            (*update2) << "release" << (*tmp).release;
            (*update2) << "release_id" << (*tmp).release_id;
            (*update2) << "artist" << (*tmp).artist;

            auto artist_oid = match_artist((*tmp).artist);

            (*update2) << "artist_id" << artist_oid;

            (*update2) << close_document;

            delete tmp;
            {
                try {
                auto entry = mongopool.acquire();
                auto db2 = (*entry)["radio"]["stations_copy"];
                    db2.update_one(filter->view(), update2->view());
                } catch (std::exception &e) {
                    //cout << "Update Station exception on match: " << e.what() << endl;
                    sleep(5);
                    return;
                }
            }

        }
    } else {
        //        cout << "No song provided in update" << endl;
    }
}

bsoncxx::types::b_oid match_artist (string name, bool *found) {
    boost::algorithm::trim(name);
    if (name.empty()) return bsoncxx::types::b_oid();

    mongocxx::options::find opt;
    try {
        auto entry = mongopool.acquire();
        auto artists = (*entry)["discogs"]["artists"];

        opt.projection( bsoncxx::builder::stream::document{} << "_id" << 1 << bsoncxx::builder::stream::finalize );
        {
            auto cursor = artists.find_one( bsoncxx::builder::stream::document{}
                 << "name" << name
                 << bsoncxx::builder::stream::finalize,
                 opt
                 );
            if (cursor) {
                if (found)
                    (*found) = true;
				return ((*cursor).view()["_id"].get_oid());
            }
        }
    } catch (std::exception &e) {
        return bsoncxx::types::b_oid();
    }

    return bsoncxx::types::b_oid();
}

SongDetail* match_song_ex(string name)
{
    //shared_ptr<SongDetail> ret = make_shared<SongDetail>();
    SongDetail* ret = new (std::nothrow) SongDetail;

    (*ret).found = false;
    (*ret).song_id = bsoncxx::types::b_oid();
    (*ret).release_id = bsoncxx::types::b_oid();
    (*ret).release = "";
    (*ret).artist = "";

    boost::algorithm::trim(name);
    if (name.empty()) return ret;
    if (DEBUG) cout << "Matching song: " << name << endl;
    try {
        auto entry = mongopool.acquire();
        auto songs = (*entry)["discogs"]["songs"];

        auto it = name.find(" - ");
        if (it != string::npos) {
            // split artist and song
            string artist = name.substr(0, it);
            auto it2 = name.find(" - ", it+3);
            string song = name.substr(it+3, it2);
            boost::algorithm::trim(artist);
            boost::algorithm::trim(song);
            // if (DEBUG) {
                cout << "Match Song compost:   " << name << endl;
                cout << "Artist:   " << artist << endl;
                cout << "Song:     " << song << endl;
            // }
            
            auto cursor = songs.find_one( bsoncxx::builder::stream::document{}
                    << "title" << song
                    << "join_artist" << artist
                    << bsoncxx::builder::stream::finalize
                    );
            if (cursor) {
                // if (DEBUG)
                    cout << "songs --> title, join_artist" << endl;
                (*ret).song_id = ((*cursor).view()["_id"].get_oid());
                (*ret).release_id = ((*cursor).view()["release_id"].get_oid());
                (*ret).release = (*cursor).view()["release"].get_utf8().value.to_string();
                (*ret).artist = ((*cursor).view()["join_artist"][0]).get_utf8().value.to_string();
                (*ret).found = true;
            } else {
                auto cursor = songs.find_one( bsoncxx::builder::stream::document{}
                        << "title" << song
                        << bsoncxx::builder::stream::finalize
                        );
                if (cursor) {
                    // if (DEBUG)
                        cout << "songs --> title" << endl;
                    (*ret).song_id = ((*cursor).view()["_id"].get_oid());
                    (*ret).release_id = ((*cursor).view()["release_id"].get_oid());
                    (*ret).release = ((*cursor).view()["release"]).get_utf8().value.to_string();
                    (*ret).artist = ((*cursor).view()["join_artist"][0]).get_utf8().value.to_string();
                    (*ret).found = true;
                }
            }
            if (((*ret).found) == false)
                (*ret).artist = artist;

        } else {
            // if (DEBUG)
                cout << "[search with total as title] songs --> title" << endl;
            auto cursor = songs.find_one( bsoncxx::builder::stream::document{}
                    << "title" << name
                    << bsoncxx::builder::stream::finalize
                    );
            if (cursor) {
                (*ret).song_id = ((*cursor).view()["_id"].get_oid());
                (*ret).release_id = ((*cursor).view()["release_id"].get_oid());
                (*ret).release = (*cursor).view()["release"].get_utf8().value.to_string();
                (*ret).artist = ((*cursor).view()["join_artist"][0]).get_utf8().value.to_string();
                (*ret).found = true;
            }
        }
    } catch (std::exception &e) {
        return ret;
    }

    return ret;
}

bsoncxx::types::b_oid match_song (string name) {
    boost::algorithm::trim(name);
    if (name.empty()) return bsoncxx::types::b_oid();
    if (DEBUG) cout << "Matching song: " << name << endl;
    mongocxx::options::find opt;
    try {
        auto entry = mongopool.acquire();
        auto songs = (*entry)["discogs"]["songs"];
        boost::regex r("(.*) - (.*)");
        boost::smatch sm;
        auto it = name.find(" - ");
        if (it != string::npos) {
            string artist = name.substr(0, it);
            auto it2 = name.find(" - ", it+3);
            string song = name.substr(it+3, it2);
            if (DEBUG) {
                cout << "Match Song compost:" << name << endl;
                cout << "Artist: " << artist << " Song: " << song << endl;
            }
            boost::algorithm::trim(artist);
            boost::algorithm::trim(song);
            opt.projection( bsoncxx::builder::stream::document{} << "_id" << 1 << bsoncxx::builder::stream::finalize );
            auto cursor = songs.find_one( bsoncxx::builder::stream::document{}
                    << "title" << song
                    << "join_artist" << artist
                    << bsoncxx::builder::stream::finalize,
                    opt
                    );
            if (cursor) {
                return ((*cursor).view()["_id"].get_oid());
            }
            cursor = songs.find_one( bsoncxx::builder::stream::document{}
                    << "title" << song
                    << bsoncxx::builder::stream::finalize,
                    opt
                    );
            if (cursor) {
                return ((*cursor).view()["_id"].get_oid());
            }
        } else {
            auto cursor = songs.find_one( bsoncxx::builder::stream::document{}
                    << "title" << name
                    << bsoncxx::builder::stream::finalize,
                    opt
                    );
            if (cursor) {
                return ((*cursor).view()["_id"].get_oid());
            }
        }
    } catch (std::exception &e) {
        cout << "miss matched : " << name << endl;
        return bsoncxx::types::b_oid();
    }


    cout << "miss matched : " << name << endl;
    /*
    try {
        opt.projection( bsoncxx::builder::stream::document{} << "_id" << 1
                << "score" << open_document << "$meta" << "textScore"  << close_document
                << bsoncxx::builder::stream::finalize );
        opt.sort ( bsoncxx::builder::stream::document{} << "score" << open_document << "$meta" << "textScore"  << close_document
                    << bsoncxx::builder::stream::finalize);
        auto cursor = disco.find_one( bsoncxx::builder::stream::document{}
                << "$text" << open_document << "$search" <<  name << close_document
                << bsoncxx::builder::stream::finalize,
                opt
                );
        if (cursor) {
            //        cout << "Song matched: " << bsoncxx::to_json((*cursor).view()) << endl;
            return ((*cursor).view()["_id"].get_oid());
        }

    } catch (std::exception &e) {
        cout << "***** Exception match_song: " << e.what() << endl;
    }
    */
    return bsoncxx::types::b_oid();
}



void zone (void) {
	ifstream ifs;
	ifs.open("db");
    string buffer;
    unsigned int count=0;
    mongocxx::instance inst{};
    mongocxx::client conn{mongocxx::uri{}};
    auto db = conn["radio"]["stations_copy"];

    while (getline(ifs, buffer)) {
        string url, furl, title;
        url = buffer.data()+10;
        furl = buffer.data()+309;
        title = buffer.data()+609;
        boost::algorithm::trim(url);
        boost::algorithm::trim(furl);
        boost::algorithm::trim(title);
        cout << "URL: " << url << " fURL: " << furl << " Title: " << title << endl;
        count++;
        bsoncxx::builder::stream::document document{};
        document << "url" << url << "furl" << furl << "title" << title;
        db.insert_one(document.view());
    }
}

pair<string,string> curl_get (shared_ptr<Station> ptr) {
  CURL *curl;
  CURLcode res;
  pair<string, int> buffer, header;
  string url = ptr->url;

  curl = curl_easy_init();
  if(curl) {
    struct curl_slist *cab = NULL;
    cab = curl_slist_append(cab, "User-Agent: Mozilla");
    cab = curl_slist_append(cab, "Icy-MetaData: 1");
    header.second = 8192;
    if (ptr->icy) buffer.second = ptr->icy;
    else buffer.second = 8192;

    if (DEBUG) cout << "Starting curl_get URL: " << url << endl;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&buffer);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, (void *)&header);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_STDERR, NULL);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, cab);

    res = curl_easy_perform(curl);
    if (res != CURLE_OK && DEBUG) {
        cout << "Error running curl: " << curl_easy_strerror(res) << endl;
    }
    curl_easy_cleanup(curl);
  }
  return {header.first, buffer.first};
}

size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    std::pair<string, int> *par = (pair<string, int> *)userp;
    if (DEBUG) {
        cout << "WriteMemoryCallback size=" << size*nmemb << " remaining=" << par->second << endl;
    }
    par->first.append((char *)contents, size*nmemb);
    par->second -= size*nmemb;
    if (par->second <= 0) {
        return 0;
    }
    return size*nmemb;
}

bool isProperUrl(string url)
{
    if (strlen(url.c_str()) == 0)
        return false;

    string targetUrl;

    auto it = url.find(",");

    if (it == string::npos)
        targetUrl = url;
    else
        targetUrl = url.substr(0, it);

    bool bValid = true;
    bool bDot = false;

    for (int i = 0; i < targetUrl.length(); i++)
    {
        char c = targetUrl[i];

        if (isalpha(c)) continue;
        if (isdigit(c)) continue;

        if (c == '.')
        {
            bDot = true;
            continue;
        }

        if (c == '~' || c == '@' || c == '+' || c == '/' || c == ':' || c == '.' || c == ';' || c == '?' || c == '=' || c == '&' || c == '-' || c == '_' || c == '*' || c == '%' || c == '(' || c == ')')
        //if (c >= 33 && c <= 128)
            continue;

        bValid = false;
        break;
    }

    if (!bValid) return false;
    if (!bDot) return false;

    return true;
}

void fix_db(void)
{
  auto entry = mongopool.acquire();
    auto db = (*entry)["radio"]["stations_copy"];
    auto cursor = db.find({});

    for (auto doc : cursor) {
        //auto ptr = make_shared<bsoncxx::document::view>(doc.data(), doc.length());
        string strTmp1 = doc["stream"].get_utf8().value.to_string();
        string strTmp2 = doc["mirrors"].get_utf8().value.to_string();

        auto update2 = make_shared<bsoncxx::builder::stream::document>();
        auto filter = make_shared<bsoncxx::builder::stream::document>();

        std::transform(strTmp1.begin(), strTmp1.end(), strTmp1.begin(), ::tolower);
        std::transform(strTmp2.begin(), strTmp2.end(), strTmp2.begin(), ::tolower);

        boost::algorithm::trim(strTmp1);
        boost::algorithm::trim(strTmp2);

        if (strTmp2.length() > 0)
        {
            auto it = strTmp2.find(",");

            if (it != string::npos)
                strTmp2 = strTmp2.substr(0, it);
        }

        (*filter) << "_id" << doc["_id"].get_oid();

        if (isProperUrl(strTmp1) == false &&
            isProperUrl(strTmp2) == false)
            {
                db.delete_one(filter->view());
                continue;
            }

        if (regex_match_ex(strTmp1, ("http:(.*)")) ||
            regex_match_ex(strTmp1, ("https:(.*)")) )
            {
                (*update2) << "$set" << open_document;
                (*update2) << "cleaned_url" << strTmp1;
                (*update2) << close_document;

                auto entry = mongopool.acquire();

                db.update_one(filter->view(), update2->view());

                continue;
            }

         if (regex_match_ex(strTmp2, ("http:(.*)")) ||
            regex_match_ex(strTmp2, ("https:(.*)")) )
            {
                (*update2) << "$set" << open_document;
                (*update2) << "cleaned_url" << strTmp2;
                (*update2) << close_document;

                auto entry = mongopool.acquire();

                db.update_one(filter->view(), update2->view());

                continue;
            }

        if (regex_match_ex(strTmp1, ("(.*)://(.*)")) == false)
        {
                (*update2) << "$set" << open_document;
                (*update2) << "cleaned_url" << "http://" + strTmp1;
                (*update2) << close_document;

                auto entry = mongopool.acquire();

                db.update_one(filter->view(), update2->view());

                continue;
        }

        if (regex_match_ex(strTmp2, ("(.*)://(.*)")) == false)
        {

                (*update2) << "$set" << open_document;
                (*update2) << "cleaned_url" << "http://" + strTmp2;
                (*update2) << close_document;

                auto entry = mongopool.acquire();

                continue;
        }

        cout << "To be delted : " << strTmp1 << " , "  << strTmp2 << " , " << doc["_id"].get_oid().value.to_string() << endl;

        db.delete_one(filter->view());
    }

    entry = mongopool.acquire();
    db = (*entry)["radio"]["stations_copy"];

    cursor = db.find({});

    for (auto doc : cursor)
    {
        string keyword = doc["cleaned_url"].get_utf8().value.to_string();

        auto filter = make_shared<bsoncxx::builder::stream::document>();

        (*filter) << "cleaned_url" << keyword;
        (*filter) << "_id" << open_document << "$ne" << doc["_id"].get_oid() << close_document;

        db.delete_many(filter->view());
    }

    cout << " Operation Finished " << endl;
}
