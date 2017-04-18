var bls = require("bls");

var config = {
	log_path : "log/bls_edge.log",
    log_level : 3,
	max_client_num : 20,
	port : 8900,
	ping_pong_time : 10,
};

var edged_stream = {};

var result = bls.start_server(config, function(client){
	console.log("client come on! id: %s", client.client_id);

	client.on("connect", function(trans_id, connect_info)
	{
		console.log("new client connect. tsid: %d connect_info: %j", 
			trans_id, connect_info);
		client.accept(true);
		//client.accept(false, "NetConnection.Connect.Rejected", "hehe");
	});

	client.on("close", function(close_status)
	{
		console.log("client close ", close_status);
	});

	client.on("play", function(trans_id, cmd_obj, stream_name){
		console.log("client call play. tsid: %d cmd_objs: %j stream_name: %s",
			trans_id, cmd_obj, stream_name);

        //if the stream has beed pull from src host,
        //then it can play directly.
        if (edged_stream[stream_name]) {
            console.log("play %s directly", stream_name);
		    client.play(trans_id, stream_name);
        } else {
            console.log("need pull from src host");
            pull_stream_from_src(stream_name, function(res) {
                if (res) {
                    edged_stream[stream_name] = true;
                    client.play(trans_id, stream_name);
                } else {
                    console.log("pull stream fail");
                    client.close();
                    console.log("debug");
                }
            });
        }
	});

	client.on("unplay", function(){
		console.log("client unplay stream......");
	});

	// setTimeout(function(){
	// 	client.close();
	// }, 5000);
});

function pull_stream_from_src(stream_name, cb_func){
    //connect remote src BLS first
	bls.remote_connect("127.0.0.1", 8956, function(edge_connect){

        //TCP connect success
		if(edge_connect)
		{
			console.log("connect remote server success.")

            var has_return = false;

            //you can send custom infomations to source BLS when doing RTMP connect
			edge_connect.connect({info:"custom info"}, function(results){
				console.log("send connect to remote server. recv:");
				console.dir(arguments);

				edge_connect.edge(stream_name, function(){
                    //Note: If this edge client is closed when waiting for edge result, 
                    //this call back will never be triggled.
                    console.log("edge complete");

                    //call back success
                    cb_func(true);
                    has_return = true;
                });
			});

            edge_connect.on("close", function(){
                console.log("edge for %s close", stream_name);
                
                if (!has_return) {
                    cb_func(false);
                }
            });
		}
		else
		{
			console.log("connect remote server fail");
            cb_func(false);
		}
	});

}
