%%% DUCKY %%%
%% MUST INSTALL "sockets" package with "pkg install -forge sockets"

function soc = rtl_sdr_connect(address)
% default address is 127.0.0.1

    if nargin<1 
	soc = socket(AF_INET,SOCK_STREAM);
	server_info = struct("addr", "127.0.0.1", "port", 1234);
	soc_status = connect(soc, server_info);

    else
	soc = socket(AF_INET,SOCK_STREAM);
	server_info = struct("addr", address, "port", 1234);
	soc_status = connect(soc, server_info);
    end
    
