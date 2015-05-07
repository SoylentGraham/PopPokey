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
	
public:
};


/*
typedef vec2x<int>	vec2i;

class TPokeyEvent
{
	SoyRef	mPokeyRef;
	int		mSerial;
	int		mPin;
};

class TPokey : public SoyWorkerThread
{
public:
	TPokey(SoyRef Ref,const std::string& Address);
	
	inline bool		operator==(const SoyRef& Ref) const	{	return mRef == Ref;	}
	
	virtual bool	Iteration() override;

	bool			Connect();
	void			Recv();
	
public:
	std::string		mAddress;
	SoyRef			mRef;
	SoySocket		mSocket;
	int				mSerial;
	
	SoyEvent<const TPokeyEvent>	mOnPinData;
};


class TGridEvent
{
public:
	vec2i		mPosition;
};
	
*/

class TPokeyMeta
{
public:
	TPokeyMeta() :
		mSerial		( -1 )
	{
	}
	
public:
	std::string		mAddress;
	int				mSerial;
	SoyRef			mChannelRef;
};

class TPollPokeyThread : public SoyWorkerThread
{
public:
	TPollPokeyThread(TChannelManager& Channels);

	virtual bool		Iteration() override;

	void				AddPokeyChannel(SoyRef ChannelRef)
	{
		mPokeyChannels.PushBack( ChannelRef );
	}
	
public:
	void				SendGetDeviceMeta();
	void				SendGetUserMeta();
	void				SendJob(TJob& Job);
	
private:
	Array<SoyRef>		mPokeyChannels;
	TChannelManager&	mChannels;
};


//	every N secs look for new pokeys
class TPokeyDiscoverThread : public SoyWorkerThread
{
public:
	TPokeyDiscoverThread(std::shared_ptr<TChannel>& Channel);
	
	virtual bool	Iteration() override;
	virtual std::chrono::milliseconds	GetSleepDuration()	{	return std::chrono::milliseconds(1000);	}

	std::shared_ptr<TChannel>&	mChannel;
};

class TPopPokey : public TJobHandler, public TChannelManager
{
public:
	TPopPokey();
	
	virtual void	AddChannel(std::shared_ptr<TChannel> Channel) override;

	void			OnInitPokey(TJobAndChannel& JobAndChannel);
	void			OnDiscoverPokey(TJobAndChannel& JobAndChannel);
	void			OnPopGridEvent(TJobAndChannel& JobAndChannel);
	void			OnUnknownPokeyReply(TJobAndChannel& JobAndChannel);
	
	TPokeyMeta		FindPokey(const TPokeyMeta& Pokey);
	
public:
	Soy::Platform::TConsoleApp	mConsoleApp;

	std::shared_ptr<TPokeyDiscoverThread>	mDiscoverPokeyThread;
	std::shared_ptr<TPollPokeyThread>	mPollPokeyThread;
	
	std::shared_ptr<TChannel>	mDiscoverPokeyChannel;
	
	Array<TPokeyMeta>			mPokeys;
};



