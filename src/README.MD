# Parser

## Mongo Database:

Database discogs have the full import from discogs, plus a collection named songs that will be searched in matches:

{
        "_id" : ObjectId("57c07cc52dc25bc026c9b960"),
        "join_artist" : [
                "The Persuader"
        ],
        "extra_artists" : [
                "Jesper Dahlbäck"
        ],
        "release" : "Stockholm",
        "release_id" : ObjectId("57beb7786e3f65b19efbd943"),
        "title" : "Östermalm",
        "duration" : "4:45"
}

In this database the release_id field is the ObjectID from the releases collection that have full release data.

Database radio have a collection named: “stations” where you will find the station data, like this one:

{
        "_id" : ObjectId("57bf2c93549d0e6243505fca"),
        "url" : "208.43.42.27:8027",
        "furl" : "",
        "title" : "",
        "response" : true,
        "type" : "shoutcast",
        "now_id" : ObjectId("57c0f0202dc25bc0264899dd"),
        "now" : "Radiomix020616 - LAS"
}

In this the now_id is the ObjectID from the discogs.songs collection


## You can run the parser in three modes:

  --help                produce help message
  --online              parse all online radios
  --all                 parse all radios
  --url arg             parse url
  --song arg            match song
  --fixdb               remove bad links and duplicated links
  --ids                 remove bad links and duplicated links


## When run in the online or all mode the software will keep running and search stations, there are two queues in the software, one is the URL queues to parse, and other is the database queue to match songs, when a station is parsed it’s “now playing” information is passed to the other queue.

The parser software will connect to the database using UNIX sockets so it doesn’t push much the OS network stack, some configuration can be done in the parser.cpp file in the following lines:

const unsigned int NUMTHREADS = 768;   // cURL threads to parse stations
const unsigned int NUMTHREADS_MONGO = 32; // database threads to process matches
bool DEBUG = false; // print debug information


## You can recompile the software after changes running the make command.

### install mongodb and other libs on ubuntu

https://docs.mongodb.com/manual/tutorial/install-mongodb-on-ubuntu/
sudo apt-get install libstdc++6
https://cmake.org/install/
http://mongodb.github.io/mongo-cxx-driver/mongocxx-v3/installation/
sudo apt-get install libboost-dev
sudo apt-get install libcurl-dev
sudo apt-get install libcurl4-openssl-dev
sudo apt-get install libboost-all-dev

### run mongodb service

sudo service mongod start
sudo service mongod stop
sudo service mongod restart

### and then

make
