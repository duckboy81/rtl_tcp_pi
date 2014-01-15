%%% DUCKY %%%
%% MUST INSTALL "sockets" package with "pkg install -forge sockets"

function client_send = rtl_sdr_connect_port(port)

soc_send = socket(AF_INET,SOCK_STREAM)

display("Trying to bind");
bind(soc_send, port)

display("Trying to listen");
r = listen(soc_send, 0)

display("Trying to accept");
[client_send, info] = accept(soc_send)

%for testing only
%while(1)
%display("Trying to send! Pausing...")
%pause

%send(client, uint8(84))

%display("Sent! Pausing...")
%pause
%end
