conn = new Mongo();
db = conn.getDB("discogs");
//cursor = db.releases.find({}).limit(2);
cursor = db.releases.find({});
while (cursor.hasNext()) {
    i = cursor.next();
    var jsonData = {};
//    print ("Artists:");
    jsonData["join_artist"] = new Array;
    i["artistJoins"].forEach(function (j) {
//        print (j["artist_name"]);
        jsonData["join_artist"].push(j["artist_name"]);
    });

//    print ("Extra Artists:");
    jsonData["extra_artists"] = new Array;
    i["extraartists"].forEach(function (j) {
//        print (j["artist_name"]);
        jsonData["extra_artists"].push(j["artist_name"]);
    });
    jsonData["release"] = i["title"];
    jsonData["release_id"] = i["_id"];
    i["tracklist"].forEach(function(j) {
        var tmp = jsonData;
        tmp["title"] = j["title"];
        tmp["duration"] = j["duration"];
        db.songs.insert(tmp);
    });
//    printjson( cursor.next()["tracklist"] );
}
