classdef jaudio < hgsetget
	%JAUDIO Java-based buffered sound playback device class
	%   JAUDIO class enhances built-in SOUND and SOUNDSC functions to allow
	%   uninterrupted playback.
   %
   %   H = JAUDIO returns an handle object to interface with Java Sound
   %   API (java.sound.sampled).
   %
   %   H = JAUDIO(FS) configures the Java Sound in output mode with
   %   sampling frequency FS in Hz. If not specified, the default rate is
   %   48 kHz.
   %
   %   H = JAUDIO(FS,NCH) sets the number of output channels. Default is 1.
   %   When NCH=2 (stereo), Channel 1 corresponds to LEFT speaker.
   %
   %   H = JAUDIO(FS,NCH,BITS) using BITS bits/sample if possible. Most
   %   platforms support BITS = 8 or 16. Default is BITS = 8.
   %
   %   H = JAUDIO(FS,NCH,BITS,NBUFF) specifies the output buffer size per
   %   channel.
   % 
   %   Example (to compliment the example in sound function):
   %      load handel
   %      N = floor(numel(y)/Fs);
   %      y = reshape(y(1:N*Fs),Fs,N);
   %      ja = jaudio(Fs);
   %      ja.open;
   %      for k = 1:N
   %         ja.play(y(:,k));
   %         pause(0.8);
   %      end
   %      ja.close;
   %      delete(ja)
   %   You should hear a snippet of Handel's Hallelujah Chorus. The data are
   %   divided up into 8 1-second segments and are queued 1 at a time. The
   %   delay between segments is made shorter than 1 s to guarantee
   %   uninterrupted playback.
   %
   %   JAUDIO Methods:
	%      open    - open the jaudio device to play back
	%      close   - close the jaudio device
	%      play    - send audio data for playback
	%      playsc  - autoscale then send data for playback
   %      wait    - block until all buffered audio data are output
	%      delete  - delete object
	%
	%   JAUDIO Properties:
   %      BitsPerSample     - number of bits/sample
   %      BufferSize        - output buffer size/channel
   %      NumberOfChannels  - number of channels
   %      Running           - (Read-Only) ['on'|{'off'}]
	%      SampleRate        - sampling frequency (Hz)
   %      SamplesOutput     - (Read-Only) number of samples remaining to output
   %
	%   See also sound and soundsc.
	
   % Version 1.0 (Aug. 05, 2010)
   % Written by: Takeshi Ikuma
   % Created: 08/05/2010
   %
   % Revision History:
   %  v.1.0 (Aug. 05, 2010) - Initial Release
	
	properties (Dependent=true)   % modifiable properties
		SampleRate     % The number of samples played or recorded per second.
		BitsPerSample  % The number of bits in each sample of a sound.
		NumberOfChannels % The number of audio channels in this format (1 for mono, 2 for stereo).
		BufferSize % The maximum number of bytes of data that will fit in the internal buffer.
	end
	
	properties (Dependent=true,SetAccess=private)   % read-only properties
		SamplesOutput % The number of samples remaining in the internal buffer.
		Running % Timer object running status.
	end
	
	methods
		function obj = jaudio(fs,nch,nbits,nbuff) % constructor
			
			error(nargchk(0,4,nargin));
			
			if nargin<1, fs = []; end
			if nargin<2, nch = []; end
			if nargin<3, nbits = []; end
			if nargin<4, nbuff = []; end
			
			import javax.sound.sampled.*
			import java.nio.ByteOrder
			
			% default configuration
			obj.sampleRate = 48000;
			obj.sampleSizeInBits = 8;
			obj.channels = 1;
			obj.bigEndian = ByteOrder.nativeOrder() == ByteOrder.BIG_ENDIAN;
			obj.isOpen = false;
			
			try
				format = AudioFormat(obj.sampleRate,obj.sampleSizeInBits,obj.channels,true,obj.bigEndian);
				obj.dataLine = AudioSystem.getSourceDataLine(format);
				obj.dataLine.open();
				obj.dataLine.close();
			catch ME
				error(obj.disp_jerr(ME.message));
			end
			
			obj.bufferSize = obj.dataLine.getBufferSize()/format.getFrameSize();
			
			switch format.getFrameSize()/format.getChannels()
				case 1
					obj.dataType = 'int8';
				case 2
					obj.dataType = 'int16';
				case 4
					obj.dataType = 'int32';
				otherwise
					error('jaudio:IncompatibleFrameSize','JAUDIO frame size is not compatible with MATLAB.');
			end
			
			% set user defined parameters
			try
				if ~isempty(fs)
					obj.SampleRate = fs;
				end
				if ~isempty(nch)
					obj.NumberOfChannels = nch;
				end
				if ~isempty(nbits)
					obj.BitsPerSample = nbits;
				end
				if ~isempty(nbuff)
					obj.BufferSize = nbuff;
				end
			catch ME
				ME.throwAsCaller
			end
		end
		
		function delete(obj) 
         % DELETE   Delete jaudio
			obj.close();
		end
		
      function open(obj)
         %OPEN   Open Java audio data line.
         %
         %   OPEN(OBJ) starts the audio playback, represented by the jaudio object,
         %   OBJ. Use the JAUDIO function to create a jaudio object, and use CLOSE
         %   function to stop the playback.
         %
         %   OPEN sets the Running property of the jausio object, OBJ, to 'On',
         %   opens Java Audio DataLine, and starts playing the sound samples in the
         %   internal buffer whenever samples are queued.
         %
         %   See also jaudio, jaudio/close, jaudio/play, jaudio/playsc.
         import javax.sound.sampled.*
         if (obj.isOpen), return; end
         format = AudioFormat(obj.sampleRate,obj.sampleSizeInBits,obj.channels,true,obj.bigEndian);
         obj.dataLine.open(format,obj.bufferSize*format.getFrameSize());
         obj.dataLine.start();
			obj.isOpen = true;
		end
		
		function close(obj)
         %CLOSE Close Java audio data line.
         %
         %   CLOSE(OBJ) stops the audio playback, represented by the jaudio object,
         %   OBJ. Use the JAUDIO function to create a jaudio object.
         %
         %   CLOSE sets the Running property of the timer object, OBJ, to 'Off',
         %   wait until all existing sound samples are played, and stop accepting
         %   further sound samples.
         %
         %   See also jaudio, jaudio/open, jaudio/play, jaudio/playsc.
         if obj.isOpen
            obj.dataLine.flush();
            obj.dataLine.stop();
            obj.dataLine.close();
            obj.isOpen = false;
         end
		end
		
		function play(obj,y)
         %PLAY   Play vector as sound.
         %   PLAY(OBJ,Y) sends the signal in vector Y out to the speaker via the
         %   jaudio object, represented by OBJ. Values in Y are assumed to be in the
         %   range -1.0 <= y <= 1.0. Values outside that range are clipped.  Stereo
         %   sounds are played, on platforms that support it, when Y is an N-by-2
         %   matrix. 
         % 
         %  See also jaudio, jaudio/open, jaudio/close, jaudio/playsc, sound.
			error(nargchk(2,2,nargin));
			if ~obj.isOpen
				error('jaudio:play:NotOpen','JAUDIO device has not been opened.');
			end
			if ~isnumeric(y) || any(isinf(y(:))) || any(isnan(y(:)))
				error('jaudio:play:InvalidInput','Y must be numeric type.');
			end
			
			SZ = size(y);
			I = find(SZ==obj.channels,1);
			if isempty(I) && sum(SZ~=1)~=1
				error('jaudio:play:InvalidInput','Y must be a vector (samples interleaved) or one dimension of Y must equal NumberOfChannels.');
			end
			
			if isempty(I) % if Y is a vector
				N = numel(y)/obj.channels;
				if N~=floor(N)
					error('jaudio:play:InvalidInput','Y must provide the same number of samples for all channels.');
				end
			else
				% bring the channel dimension first then vectorize
				y = reshape(permute(y,[I 1:I-1 I+1:numel(SZ)]),numel(y),1);
			end
			
			if any(abs(y)>1)
				warning('jaudio:play:OutOfRange','Some samples of Y are out of range.');
			end
			
			% first convert data from (-1,1), then change data type to that of
			% the java data format, then obtain the byte array representation
			% by typecasting
			data = typecast(cast(y*2^(obj.sampleSizeInBits-1),obj.dataType),'int8');
			
			% write data
			obj.dataLine.write(data, 0, numel(data));
		end
		
		function playsc(obj,y,slim)
         %PLAYSC Autoscale and play vector as sound.
         %   PLAYSC(OBJ,Y) is the same as PLAY(OBJ,Y) except the data is scaled so
         %   that the sound is played as loud as possible without clipping. The mean
         %   of the dynamic range of the data is set to zero after the
         %   normalization.
         %
         %   PLAYSC(OBJ,Y,SLIM) where SLIM = [SLOW SHIGH] linearly scales values in
         %   Y in the range [SLOW, SHIGH] to [-1, 1].  Values outside this range are
         %   not clipped.  By default, SLIM is [MIN(Y) MAX(Y)].
         %
         %  See also jaudio, jaudio/open, jaudio/close, jaudio/play, soundsc.
			error(nargchk(2,3,nargin));
			
			if nargin<3
				slim = [min(y(:)) max(y(:))];
			else
				if numel(slim)~=2 || ~isnumeric(slim) || any(isinf(slim)) || any(isnan(slim)) || ~all(isreal(slim))
					error('jaudio:playsc:InvalidInput','SLIM must be real 2-element numeric vector');
				end
				if slim(2)<=slim(1)
					error('jaudio:playsc:InvalidInput','SLIM(2) must be strictly greater than SLIM(1)');
				end
			end
			
			% Scale the data so that the limits in
			% SLIM are scaled to the range [-1 +1]
			
			if all(slim==0)
				y(:) = 0;
			else
				dy=diff(slim);
				y(:) = (y-slim(1))/dy*2-1;
			end
			
			% Play the scaled sequence:
			obj.play(y);
		end
		
		function wait(obj)
			obj.dataLine.drain();
		end
		
		function set.SampleRate(obj,fs)
			error(nargchk(2,2,nargin));
			
			if obj.isOpen
				error('jaudio:set:PortOpen','SampleRate cannot be set when open.');
			end
			
			if numel(fs)~=1 || ~isnumeric(fs) || isinf(fs) || isnan(fs) || fs<=0
				error('jaudio:setSampleRate:InvalidInput','SampleRate must be a positive scalar value.');
			end
			
			fs_old = obj.sampleRate;
			obj.sampleRate = fs;

			msg = obj.checkformat();
			if ~isempty(msg) % true, failed
				obj.sampleRate = fs_old;
				error(msg);
			end
		end
		
		function set.BitsPerSample(obj,nbits)
			error(nargchk(2,2,nargin));

			if obj.isOpen
				error('jaudio:set:PortOpen','BitsPerSample cannot be set when open.');
			end
			
			if numel(nbits)~=1 || ~isnumeric(nbits) || isinf(nbits) || isnan(nbits) || nbits<=0 || floor(nbits)~=nbits
				error('jaudio:setBitsPerSample:InvalidInput','BitsPerSample must be a positive integer scalar value.');
			end
			
			nbits_old = obj.sampleSizeInBits;
			obj.sampleSizeInBits = nbits;
			
			msg = obj.checkformat();
			if ~isempty(msg) % true, failed
				obj.sampleSizeInBits = nbits_old;
				error(msg);
			end
		end
		
		function set.NumberOfChannels(obj,nch)
			error(nargchk(2,2,nargin));

			if obj.isOpen
				error('jaudio:set:PortOpen','NumberOfChannels cannot be set when open.');
			end
			
			if numel(nch)~=1 || ~isnumeric(nch) || isinf(nch) || isnan(nch) || nch<=0 || floor(nch)~=nch
				error('jaudio:setNumberOfChannels:InvalidInput','NumberOfChannels must be a positive integer scalar value.');
			end
			
			nch_old = obj.channels;
			obj.channels = nch;

         nbuff = obj.bufferSize;
			obj.bufferSize = nbuff*nch;
			
			msg = obj.checkformat();
			if ~isempty(msg) % true, failed
				obj.channels = nch_old;
            obj.bufferSize = nbuff;
				error(msg);
			end
		end
		
		function set.BufferSize(obj,nbuff) % per channel
			error(nargchk(2,2,nargin));

			if obj.isOpen
				error('jaudio:set:PortOpen','BufferSize cannot be set when open.');
			end
			
			if numel(nbuff)~=1 || ~isnumeric(nbuff) || isinf(nbuff) || isnan(nbuff) || nbuff<=0 || floor(nbuff)~=nbuff
				error('jaudio:setBufferSize:InvalidInput','BufferSize must be a positive integer scalar value.');
			end
			
			nbuff_old = obj.bufferSize;
			obj.bufferSize = nbuff;
			
			msg = obj.checkformat();
			if ~isempty(msg) % true, failed
				obj.bufferSize = nbuff_old;
				error(msg);
			end
		end
		
		function fs = get.SampleRate(obj)
			fs = obj.sampleRate;
		end
		
		function nbits = get.BitsPerSample(obj)
			nbits = obj.sampleSizeInBits;
		end
		
		function nch = get.NumberOfChannels(obj)
			nch = obj.channels;
		end
		
		function nbuff = get.BufferSize(obj)
			nbuff = obj.bufferSize;
		end
		
		function N = get.SamplesOutput(obj)
			if isempty(obj.dataLine), N = 0;
			else N = obj.bufferSize-obj.dataLine.available();
			end
		end
		
		function running = get.Running(obj)
			if obj.isOpen, running = 'yes';
			else running = 'no';
			end
		end
	end
	
	methods (Access=private)
		function msg = checkformat(obj)
			import javax.sound.sampled.*
			
			msg = '';
			try
				format = AudioFormat(obj.sampleRate,obj.sampleSizeInBits,obj.channels,true,obj.bigEndian);
				obj.dataLine = AudioSystem.getSourceDataLine(format);
				obj.dataLine.open(format,obj.bufferSize*format.getFrameSize());
			catch ME
				msg = obj.disp_jerr(ME.message);
			end
			
			% if success get the actual buffer size & data format
			if isempty(msg)
				obj.bufferSize = obj.dataLine.getBufferSize()/format.getFrameSize();
				
				switch format.getFrameSize()/format.getChannels()
					case 1
						obj.dataType = 'int8';
					case 2
						obj.dataType = 'int16';
					case 4
						obj.dataType = 'int32';
					otherwise
						msg = 'JAUDIO frame size is not compatible with MATLAB.';
				end
			end
			
			obj.dataLine.close();
		end
		
		function msg = disp_jerr(obj,msg) %#ok
			I = strfind(msg,char(10));
			if numel(I)>1, msg = msg(I(1)+1:I(2)-1); end
		end
	end
	
	properties (Access=private)
		sampleRate			% sampling rate in samples/s
		sampleSizeInBits	% number of bits/sample
		channels		% number of channels
		bigEndian	% data endianness
		bufferSize	% line buffer
		dataType % data format for java
		
		isOpen	% true if line is open
		
		dataLine	% javax.sound.SourceDataLine object
	end
end

% Copyright (c)2010, Takeshi Ikuma
% All rights reserved.
%
% Redistribution and use in source and binary forms, with or without
% modification, are permitted provided that the following conditions are
% met:
%
%   * Redistributions of source code must retain the above copyright
%   notice, this list of conditions and the following disclaimer. *
%   Redistributions in binary form must reproduce the above copyright
%   notice, this list of conditions and the following disclaimer in the
%   documentation and/or other materials provided with the distribution.
%   * Neither the names of its contributors may be used to endorse or
%   promote products derived from this software without specific prior
%   written permission.
%
% THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
% IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
% THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
% PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
% CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
% EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
% PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
% PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
% LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
% NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
% SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
