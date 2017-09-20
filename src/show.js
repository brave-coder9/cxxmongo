conn = new Mongo();
db = conn.getDB("radio");
db2 = conn.getDB("discogs");
cursor = db.stations.find({$and : [{"now_id" : { "$ne" : ObjectId("000000000000000000000000") }},{"now_id" : { "$exists" : true }}]}).limit(20);
while (cursor.hasNext()) {
    i = cursor.next();
    print ("URL: " + i["url"]);
    print ("Now: " + i["now"]);
    print ("Now ID: " + i["now_id"]);
    var json = {};
    json["_id"] = i["now_id"];
    cursor2 = db2.songs.findOne(json);
    print ("Song data: ");
    printjson (cursor2);
}   
