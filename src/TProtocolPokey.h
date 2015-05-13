#pragma once
#include <ofxSoylent.h>
#include <SoyApp.h>
#include <TJob.h>
#include <TChannel.h>
#include <TChannelSocket.h>
#include <SoyMath.h>



namespace TPokeyCommand
{
	enum Type : unsigned char
	{
		//	not real codes for pokeys, but we need commands here
		Invalid					= 0xff,
		UnknownReply			= 0xfe,
		Discover				= 0xfd,
	
		//	real codes
		GetDeviceMeta			= 0x00,
		GetUserId				= 0x03,
		GetDeviceState			= 0xCC,
	};
	DECLARE_SOYENUM( TPokeyCommand );
	
	unsigned char	CalculateChecksum(const unsigned char* Header7);
};





class TProtocolPokey : public TProtocol
{
public:
	static std::atomic<unsigned char>	mRequestCounter;	//	gr: per device, but establish when this resets
	
public:
	TProtocolPokey()
	{
	}
	
	virtual TDecodeResult::Type	DecodeHeader(TJob& Job,TChannelStream& Stream) override;
	virtual TDecodeResult::Type	DecodeData(TJob& Job,TChannelStream& Stream) override;
	
	virtual bool		Encode(const TJobReply& Reply,std::stringstream& Output) override;
	virtual bool		Encode(const TJobReply& Reply,Array<char>& Output) override;
	virtual bool		Encode(const TJob& Job,std::stringstream& Output) override;
	virtual bool		Encode(const TJob& Job,Array<char>& Output) override;
	
	virtual bool		FixParamFormat(TJobParam& Param,std::stringstream& Error) override;
	
	bool				DecodeReply(TJob& Job,const BufferArray<unsigned char,64>& Data);
	bool				DecodeGetDeviceStatus(TJob& Job,const BufferArray<unsigned char,64>& Data);

public:
};


