#pragma once
#include <ofxSoylent.h>
#include <SoyApp.h>
#include <TJob.h>
#include <TChannel.h>
#include <TChannelSocket.h>
#include <SoyMath.h>






class TProtocolPokey : public TProtocol
{
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



class TPollPokeyThread : public SoyWorkerThread
{
public:
	TPollPokeyThread(TChannelManager& Channels);

	virtual bool		Iteration() override;

	void				AddPokeyChannel(SoyRef ChannelRef)
	{
		mPokeyChannels.PushBack( ChannelRef );
	}
	
	Array<SoyRef>		mPokeyChannels;
	TChannelManager&	mChannels;
};



class TPopPokey : public TJobHandler, public TChannelManager
{
public:
	TPopPokey();
	
	virtual void	AddChannel(std::shared_ptr<TChannel> Channel) override;

	void			OnInitPokey(TJobAndChannel& JobAndChannel);
	void			OnPopGridEvent(TJobAndChannel& JobAndChannel);
	
	
public:
	Soy::Platform::TConsoleApp	mConsoleApp;

	std::shared_ptr<TPollPokeyThread>	mPollPokeyThread;
};



