var express = require('express');
var cors = require('cors');
var shell = require('shelljs');
var bodyParser = require('body-parser');

var app = express();
//support parsing of application/json type post data
app.use(bodyParser.json());
//support parsing of application/x-www-form-urlencoded post data
app.use(bodyParser.urlencoded({ extended: false }));
/**
 * For Cross-Origin
 */
app.use(function (req, res, next) {
    res.header("Access-Control-Allow-Origin", "*");
    res.header("Access-Control-Allow-Headers", "Origin, X-Requested-With, Content-Type, Accept");
    next();
});
app.options('*', cors());
app.all('/*', function (req, res, next) {
    res.header("Access-Control-Allow-Origin", "http://localhost:3000");
    res.header('Access-Control-Allow-Methods', 'GET,PUT,POST,DELETE');
    res.header("Access-Control-Allow-Headers", "X-Requested-With,     Content-Type");
    next();
});

/**
 * hello API for test
 * @usage
 * curl -i -H "Accept: application/json" -H "Content-Type: application/json" -X GET http://localhost:3000/hello
 */
app.get('/hello', function (req, res) {
    console.log('GET /hello');
    res.header("Access-Control-Allow-Origin", "*");
    res.header("Access-Control-Allow-Headers", "Origin, X-Requested-With, Content-Type, Accept");
    res.writeHead(200, {
        'Content-Type': 'text/plain'
    });
    res.end("~~~ Hello! ~~~\n\n");
});

app.post('/station_info', function (req, res) {
    console.log("-------------------------------------------");
    console.log("station_type = " + req.body.station_type);
    console.log("now = " + req.body.now);
    console.log("now_id = " + req.body.now_id);
    console.log("artist = " + req.body.artist);
    console.log("artist_id = " + req.body.artist_id);
    console.log("release = " + req.body.release);
    console.log("release_id = " + req.body.release_id);

    res.header("Access-Control-Allow-Origin", "*");
    res.header("Access-Control-Allow-Headers", "Origin, X-Requested-With, Content-Type, Accept");
    res.writeHead(200, {
        'Content-Type': 'text/plain'
    });
    res.end("");
});

/**
 * API - /script/1
 */
app.get('/script/:index', function (req, res) {
    console.log('GET /script/' + req.params.index);
    res.header("Access-Control-Allow-Origin", "*");
    res.header("Access-Control-Allow-Headers", "Origin, X-Requested-With, Content-Type, Accept");
    res.writeHead(200, {
        'Content-Type': 'text/plain'
    });
    // execute shell script.
    var cmd = 'dir';
    if (shell.exec(cmd).code !== 0) {
        console.log('execute error cmd = ' + cmd);
        res.end('fail');
    } else {
        res.end(req.params.index + ' success');
    }
});

// var server = app.listen(process.env.PORT || 3000, 'localhost', function () {
//     var host = server.address().address;
//     var port = server.address().port;
//     console.log('service started at http://%s:%s', host, port);
// });

var server = app.listen(process.env.PORT || 3000);
console.log('Service started at http://localhost:%s', server.address().port);
