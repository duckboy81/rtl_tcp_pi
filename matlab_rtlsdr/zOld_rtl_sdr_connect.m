function soc = rtl_sdr_connect(address)
% soc = rtl_sdr_connect(address)
% 
% default address is 127.0.0.1

    import java.net.Socket
    import java.io.*


    if nargin<1
        soc = Socket('127.0.0.1',1234);
    else
        soc = Socket(address,1234);
    end
    
