var server = require("bls");

var config = {
    //log file path bls write its own log to
    log_path : "log/bls.log",

    //trace:0 debug:1 info:2 warn:3 err:4 critical:5 off:6
    //if you use low level, bls will be more busy
    log_level : 1,

    //the maximum number of clients connect to server concurrently
    max_client_num : 2000,

    //port listen to
    port : 8956,

    //the interval seconds bls uses to send heartbeat msg to clients
    ping_pong_time : 10,
};

//record the publishing stream's name
var publishing_stream = {};

//start listen and serve as RTMP server
//cb func is called when a client connects(tcp connect) to server
//client argument presents a rtmp client
server.start_server(config, function(client){
    console.log("client come on! id: %s", client.client_id);

    //the callback func when this client sends RTMP connect command
    client.on("connect", function(trans_id, connect_info)
    {
        console.log("new client connect. %s tsid: %d connect_info: %j", 
            client.client_id, trans_id, connect_info);

        client.accept(true);
        //client.accept(false, "NetConnection.Connect.Rejected", "reject");
        
        //you can send any command to client
        //if you need result from client, the callback function must be set
        client.call("needResult", [{}, {data:66}], function(res_cmd, res_args){
            console.log("get result from client %s %s", res_cmd, res_args);
        });
    });

    //this client leave
    client.on("close", function(close_status)
    {
        console.log("%s client close ", client.client_id, close_status);

        if (client.publish_stream_name) {
            delete publishing_stream[client.publish_stream_name];
        }
    });

    //register a cb func when this client wants to publish a stream.
    //note: bls just allows one client publishs one stream now.
    client.on("publish", function(trans_id, cmd_objs, stream_name)
    {
        console.log("client call publish. tsid: %d cmd_objs: %j stream_name: %s",
            trans_id, cmd_objs, stream_name);

        //if this stream name is publishing, you can not publish the same stream name
        if (!publishing_stream[stream_name]) {
            //if you allow this client to publish stream with stream_name, just need to call publish function
            //trans_id must be same with trans_id in cb arguments
            //you can custom the stream name which bls uses to publish
            client.publish(trans_id, stream_name);

            publishing_stream[stream_name] = true;
            client.publish_stream_name = stream_name;
        } else {
            client.close();
        }
    });

    //register a cb func when this client wants to play a stream
    //note: bls just allows one client play one stream now.
    client.on("play", function(trans_id, cmd_obj, stream_name){
        console.log("client call play. tsid: %d cmd_objs: %j stream_name: %s",
            trans_id, cmd_obj, stream_name);

        if (publishing_stream[stream_name]) {
            //trans_id must be same with the cb arguments
            //you can choose a stream name for this client, not must be same with the client wants
            //
            //NOTE: you can also wait for publish src ready than call play method. In this example, 
            //we just close this player.
            client.play(trans_id, stream_name);
        } else {
            client.close();
        }
    });

    //when client publishs a stream, there will be a meta data in stream data
    //meta data size should not be bigger than MAX_BUFFER_LEN, default is 2KB
    client.on("onMetaData", function(meta_data){
        console.log("get metadata %j", meta_data);
    })

    //when this client call stop play command
    client.on("unplay", function(){
        console.log("client unplay stream......");
    });

    //when this client call unplish, which means this client wants stop publish stream
    client.on("unpublish", function(){
        console.log("client unpublish stream......");
    });

    //bls sends heartbeat to client with seconds interval, which is indicated in config
    //when client send back pong msg, which is required, this cb func will be called
    //delay indicates the transport delay between bls and client
    client.on("ping_pong_request", function(delay, recv_sum){
        console.log("get pong response! %d %d", delay, recv_sum);
    });

    //listen to custom command from client, so client can send custom data to bls
    client.on("customCmd", function(trans_id, cmd_obj, data){
        console.log("get user custom command. %s %s %s", trans_id, cmd_obj, data);

        var result = ["result data"];

        //you can answer client with "_result" or "_error"
        //trans_id must be same with the one in cb func arguments
        client.result("_result", trans_id, result);
    });

    client.enable_av_cb(function(av_type, timestamp, is_sh, is_key, data){
        console.log("get a %s data. ts:%d, is sequence header:%s, is key frame:%s", 
            av_type, timestamp, is_sh, is_key);
    });
    
    setTimeout(function(){
        client.disable_av_cb()
    }, 5000);
});
