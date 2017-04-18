var bls                 = require("./build/Release/bls.node");
var util                = require("util");
var EventEmitter     = require("events").EventEmitter;
var amf                 = require("node-amfutils");

//buffer size for command data, one client uses one buffer
//do not send custom command with data bigger than the size
var MAX_BUFFER_LEN = 2*1024;

//the maximum size of a video frame
var MAX_VIDEO_BUFFER_LEN = 2*1024*1024;

var BLS_VERSION = "1.0.2";

var g_ping_pong_time = 10;

var _g_connect_cb;

var BLS_STATE = {
    WAIT_ACCEPT : 0,
    ACCEPT : 1,
    CLOSE : 2
};

/**
* class BlsClient
*/
function BlsClient(client, client_id, peer_ip)
{
    this._client = client;
    this.client_id = client_id;
    this.peer_ip = peer_ip;
    this.read_buffer = new Buffer(MAX_BUFFER_LEN);
    this.write_buffer = new Buffer(MAX_BUFFER_LEN);
    this.av_buffer = new Buffer(MAX_VIDEO_BUFFER_LEN);
    this.transid_flag = 6;
    this.transid_cb = {};
    this.connect_state = BLS_STATE.WAIT_ACCEPT;
    this.av_listener_count = 0;

    this.publish_stream_id = 0;
    this.play_stream_id = 0;
    this.stream_id_flag = 0;

    this.recv_pong_flag = true;

    var self = this;

    EventEmitter.call(this);

    this.ping_pong_timer = setInterval(function()
    {
        if(!self.recv_pong_flag)
        {
            self.close();
            return;
        }
        bls.send_ping_request(client);

        self.recv_pong_flag = false;
    }, g_ping_pong_time*1000);

    bls.bind_read_write_buf(client, this.read_buffer, this.write_buffer, this.av_buffer);
    bls.bind_on_msg_cb(client, this._on_msg_cb.bind(this));
    bls.bind_on_close_cb(client, this._on_close_cb.bind(this));
}

util.inherits(BlsClient, EventEmitter);

BlsClient.prototype._on_msg_cb = function(stream_id, is_amf0, len)
{
    var args;

    if(stream_id == "ping_pong_request")
    {
        this.recv_pong_flag = true;
        this.emit.apply(this, [stream_id, is_amf0, len]);
        return;
    }
    else if(stream_id == "play_stop_event")
    {
        this.emit.apply(this, [stream_id, is_amf0]);
        return;
    }

    try {
    if(is_amf0){
        args = amf.amf0Decode(this.read_buffer.slice(0, len));
    }
    else{
        args = amf.amf3Decode(this.read_buffer.slice(0, len));
    }
    }
    catch (err) {
        args = {err:err};
    }
    
    if(this.on_cmd_message(stream_id, args))
        this.emit.apply(this, args);
}

BlsClient.prototype._on_close_cb = function(close_status, recv_bytes, send_bytes)
{
    this.connect_state = BLS_STATE.CLOSE;
    
    clearInterval(this.ping_pong_timer);

    this.emit("close", close_status, recv_bytes, send_bytes);

    if(this.io_server && this.publish_stream_name)
    {
        this.io_server.to(this.publish_stream_name).emit("bls_close");
    }
}

BlsClient.prototype._write_msg = function(timestamp, type_id, streamid, chunk_id, msg_array)
{
    //TODO: use write buffer to improve speed
    var buf = amf.amf0Encode(msg_array);
    bls.write_message_buf(this._client, buf, streamid, timestamp, type_id, chunk_id);
}

BlsClient.prototype.accept = function(allow, code, descript)
{
    if(this.connect_state != BLS_STATE.WAIT_ACCEPT)
    {
        return;
    }

    var command_name = "_result";
    var server_info = {
        fmserver : "FMS/3,5,3,888",
        capabilities : 127,
        mode : 1
    };
    var status = {
        level : "status",
        code : "NetConnection.Connect.Success",
        description : "Connection succeeded",
        objectEncoding : 0,
        time : Math.floor(new Date().getTime() / 1000),
    };

    this.connect_state = BLS_STATE.ACCEPT;

    if(!allow)
    {
        command_name = "onStatus";
//        status.level = "status";
        status.code = code;
        status.description = descript;

        this.connect_state = BLS_STATE.CLOSE;
    }

    this._write_msg(0, 20, 0, 3, [command_name, 1, server_info, status]);
}

BlsClient.prototype.is_closed = function()
{
    return this.connect_state == BLS_STATE.CLOSE;
}

BlsClient.prototype.enable_av_cb = function(cb_func)
{
    var self = this;
    bls.enable_throwup_av(
        this._client, 
        function(av_type, is_key, is_sh, timestamp, size) {
            cb_func(av_type, timestamp, is_sh, is_key, new Buffer(self.av_buffer.slice(0, size)));
        });
}

BlsClient.prototype.disable_av_cb = function()
{
    bls.disable_throwup_av(this._client);
}

BlsClient.prototype.get_aac_sh = function()
{
    if(this.aac_sh)
        return this.aac_sh;

    var size = bls.get_aac_sh(this._client);

    if(size > 0)
    {
        this.aac_sh = new Buffer(this.av_buffer.slice(0, size));
    }

    return this.aac_sh;
}

BlsClient.prototype.get_avc_sh = function()
{
    if(this.avc_sh)
        return this.avc_sh;
    
    var size = bls.get_avc_sh(this._client);

    if(size > 0)
    {
        this.avc_sh = new Buffer(this.av_buffer.slice(0, size));
    }

    return this.avc_sh;
}

BlsClient.prototype.close = function()
{
    if(this.connect_state != BLS_STATE.CLOSE)
    {
        bls.close_client(this._client);
        this.connect_state = BLS_STATE.CLOSE;
    }
}

BlsClient.prototype.on_cmd_message = function(streamid, args)
{
    var cmd_name = args[0];
    var trans_id = args[1];
    var cmd_obj = args[2];
    var user_data = args[3];

    if(cmd_name == "pause")
    {
        if(user_data == true)
        {
            this._write_msg(0, 20, streamid, 3, ['onStatus', 0, null, {
                level : 'status',
                code  : 'NetStream.Pause.Notify',
                description : "Paused stream",
            }]);
            bls.send_stream_eof(this._client);
        }
        else
        {
            this._write_msg(0, 20, streamid, 3, ['onStatus', 0, null, {
                level : 'status',
                code  : 'NetStream.Unpause.Notify',
                description : "Unpaused stream",
            }]);
            bls.send_stream_begin(this._client);
        }
        return false;
    }

    if(cmd_name == "releaseStream")
    {
        this._write_msg(0, 20, 0, 3, ["_result", trans_id, null, undefined]);
        return true;
    }

    else if(cmd_name == "createStream")
    {
        this.stream_id_flag ++;
        while(this.stream_id_flag == this.play_stream_id || 
            this.stream_id_flag == this.publish_stream_id)
        {
            if(this.stream_id_flag > 10)
                this.stream_id_flag = 0;

            this.stream_id_flag ++;
        }
        this._write_msg(0, 20, 0, 3, ["_result", trans_id, null, this.stream_id_flag]);
        return true;
    }

    else if(cmd_name == "deleteStream")
    {
        if(user_data == this.play_stream_id)
        {
            this.emit.apply(this, ["unplay"]);
            this.play_stream_id = 0;
            this.stream_id_flag -= user_data;

            bls.unplay_stream(this._client);
        }
        if(user_data == this.publish_stream_id)
        {
            this.emit.apply(this, ["unpublish"]);
            this.publish_stream_id = 0;
            this.stream_id_flag -= user_data;

            if(this.io_server && this.publish_stream_name)
            {
                this.io_server.to(this.publish_stream_name).emit("bls_close");
            }

            bls.unpublish_stream(this._client);
        }
    }

    else if(cmd_name == "publish")
    {
        if(this.publish_stream_id == 0)
            this.publish_stream_id = streamid;
        else
            return false;
    }

    else if(cmd_name == "play")
    {
        if(this.play_stream_id == 0)
            this.play_stream_id = streamid;
        else
            return false;
    }

    else if(cmd_name == "_result" || cmd_name == "_error")
    {
        if(this.transid_cb[trans_id])
        {
            this.connect_state = BLS_STATE.ACCEPT;
            this.transid_cb[trans_id](cmd_name, args.slice(2));
            delete this.transid_cb[trans_id];
        }
    }

    else if(cmd_name == "@setDataFrame")
    {
        args.shift()
    }

    return true;
}

BlsClient.prototype.result = function(cmd_name, transid, args_array)
{
    if(this.connect_state != BLS_STATE.ACCEPT)
    {
        return;
    }

    this._write_msg(0, 20, 0, 3, [cmd_name, transid].concat(args_array));
}

BlsClient.prototype.call = function(cmd_name, args_array, cb_func)
{
    if(this.connect_state != BLS_STATE.ACCEPT)
    {
        return;
    }

    if(! cb_func)
    {
        this._write_msg(0, 20, 0, 3, [cmd_name, 0].concat(args_array));
    }
    else
    {
        this.transid_cb[this.transid_flag] = cb_func;
        this._write_msg(0, 20, 0, 3, [cmd_name, this.transid_flag].concat(args_array));
        
        if(this.transid_flag > 30)
            this.transid_flag = 6;
        else
            this.transid_flag += 1;
    }
}

BlsClient.prototype.publish = function(trans_id, stream_name)
{
    if(this.connect_state != BLS_STATE.ACCEPT)
    {
        return;
    }

    if(this.publish_stream_id == 0)
    {
        return;
    }

    if(bls.publish_stream(this._client, stream_name))
    {
        this._write_msg(0, 20, this.publish_stream_id, 3, ["onFCPublish", 0, null, {
            code : "NetStream.Publish.Start",
            description : "Started publishing stream"
        }]);
        this._write_msg(0, 20, this.publish_stream_id, 3, ["onStatus", 0, null, {
            level : "status",
            code : "NetStream.Publish.Start",
            description : "Started publishing stream"
        }]);
    }
    else
    {
        this.close();
    }
}

BlsClient.prototype.unpublish = function()
{
    bls.unpublish_stream(this._client);
}

BlsClient.prototype.play = function(trans_id, stream_name)
{
    if(this.connect_state != BLS_STATE.ACCEPT)
    {
        return;
    }

    if(this.play_stream_id == 0)
        return;

    this._write_msg(0, 20, this.play_stream_id, 3, ["onStatus", 0, null, {
        level : "status",
        code : "NetStream.Play.Reset",
        description : "Playing and resetting stream",
        details : "stream", 
        clientid : "ASAICiss",
    }]);

    this._write_msg(0, 20, this.play_stream_id, 3, ["onStatus", 0, null, {
        level : "status",
        code : "NetStream.Play.Start",
        description : "Started playing stream",
        details : "stream", 
        clientid : "ASAICiss",
    }]);

    this._write_msg(0, 20, this.play_stream_id, 3, ["|RtmpSampleAccess", false, false]);
    
    this._write_msg(0, 20, this.play_stream_id, 3, ["onStatus", {
        code : "NetStream.Data.Start",
    }]);

    bls.play_stream(this._client, this.play_stream_id, stream_name);
}

BlsClient.prototype.connect = function(client_info, cb_func)
{
    this.transid_cb[1] = cb_func;
    this._write_msg(0, 20, 0, 3, ["connect", 1, client_info]);
}

BlsClient.prototype.edge = function(stream_name, cb)
{
    var self = this;

    this.call("createStream", [null], function(result_name, res_array){

        if(!bls.publish_stream(self._client, stream_name))
        {
            self.close();
            return;
        }

        self._write_msg(0, 20, res_array[1], 3, [
            "play", 0, null, stream_name]);

        if(cb)
            cb();
    });
}

BlsClient.prototype.push = function(stream_name, cb)
{
    var self = this;

    this.call("createStream", [null], function(result_name, res_array){

        self._write_msg(0, 20, res_array[1], 3, [
            "publish", 1, null, stream_name, "live"]);
        
        bls.play_stream(self._client, res_array[1], stream_name);

        if(cb)
        {
            cb();
        }
    });
}

function _connect_cb_wraper(client, client_id, peer_ip)
{
    var bls_client = new BlsClient(client, client_id, peer_ip);
    _g_connect_cb(bls_client);
}

function start_server(conf,cb_connect_f)
{
    _g_connect_cb = cb_connect_f;

    if(conf.ping_pong_time)
    {
        g_ping_pong_time = conf.ping_pong_time;
    }
    
    bls.start_server(conf, _connect_cb_wraper);
}

function remote_connect(ip, port, cb_func)
{
    bls.remote_connect(ip, port, function(client, id){

        if(client)
        {
            var new_client = new BlsClient(client, id);
            cb_func(new_client);
        }
        else
        {
            cb_func();
        }

    });
}

function init_bls_log(conf_path, level)
{
    bls.init_logger(conf_path, level);
    return;
}

function write_log(level, format)
{
    var args = Array.prototype.slice.call(arguments);

    var temp = args[0];
    args[0] = args[1];
    args[1] = temp;

    args[0] = "[%s] " + args[0];

    var log_str = util.format.apply(this, args);

    //bls.write_log(log_str);
    bls.write_log(log_str.replace(/%/g, "@"));
}

module.exports = { 
    init_log : init_bls_log,
    write_log : write_log,
    start_server : start_server,
    remote_connect : remote_connect,
    CON_STATE : BLS_STATE, 
    MAX_BUFFER_LEN : MAX_BUFFER_LEN,
    MAX_VIDEO_BUFFER_LEN : MAX_VIDEO_BUFFER_LEN,
};
