#pragma once
#include <ofxSoylent.h>
#include <SoyApp.h>
#include <TJob.h>
#include <TChannel.h>
#include <TChannelSocket.h>
#include <SoyMath.h>

#include "TProtocolPokey.h"


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

class TPinMeta
{
public:
	TPinMeta();

public:
	vec2x<int>	mCoord;
	float		mDownDuration;	//	to detect stuck pins we increment/reset how long a pin has been held down
};

class TPokeyMeta
{
public:
	static const char*	CoordDelim;
	static const char*	CoordComponentDelim;
	static const vec2x<int>	GridCoordLaserGate;
	static const vec2x<int>	GridCoordInvalid;
	static const char*	LaserGateOnReply;
	static const char*	LaserGateOffReply;
	static float		PinDownTooLong;		//	if the pin has been down this long, ignore it
	
public:
	TPokeyMeta() :
		mSerial			( -1 ),
		mDhcpEnabled	( false ),
		mIgnored		( false )
	{
	}
	
	bool			HasBootupAddress() const { return mAddress == "10.0.0.250:20055"; }
	bool			IsValid() const	{	return mSerial != -1;	}
	bool			SetGridMap(std::string GridMapString,std::stringstream& Error);
	std::string		GetGridMapString() const
	{
		Array<vec2x<int>> PinToGridMap;
		for ( int p=0;	p<mPins.GetSize();	p++ )
		{
			PinToGridMap.PushBack( mPins[p].mCoord );
		}
		return Soy::StringJoin( GetArrayBridge(PinToGridMap), CoordDelim );
	}
	size_t			GetGridMapCount() const
	{
		return mPins.GetSize();
	}

	vec2x<int>			UpdatePins(const ArrayBridge<bool>& Pins);	//	returns coord if a pin down
	
	void				UpdatePin(size_t Pin,bool PinDown,float Delta);
	bool				IsPinIgnored(size_t Pin);		//	gr: remove double negative
	void				GetIgnoredPins(ArrayBridge<size_t>&& IgnoredPins);
	vec2x<int>			GetPinGridCoord(size_t Pin);
	float				GetPinDownDuration(size_t Pin);
	
	TPinMeta&			GetPin(size_t Pin);
	float				GetTimeSinceUpdate() const;			//	how long ago did we hear from this pokey
	
public:
	//	gr: merge these into a pin struct
	BufferArray<TPinMeta,100>	mPins;
	std::string			mAddress;
	int					mSerial;
	SoyRef				mChannelRef;
	std::string			mVersion;
	bool				mDhcpEnabled;
	bool				mIgnored;		//	gr: fix double negative!
	SoyTime				mLastUpdate;
};
std::ostream& operator<< (std::ostream &out,const TPokeyMeta &in);


class TPokeyManager
{
public:
	std::shared_ptr<TPokeyMeta>	GetPokey(const TPokeyMeta& Pokey);
	std::shared_ptr<TPokeyMeta>	GetPokey(int Serial,bool Create=false);
	std::shared_ptr<TPokeyMeta>	GetPokey(SoyRef ChannelRef);

	void	GetPokeys(ArrayBridge<std::shared_ptr<TPokeyMeta>>&& Pokeys)
	{
		std::lock_guard<std::mutex> Lock( mPokeysLock );
		Pokeys.Copy( mPokeys );
	}
	
protected:
	std::mutex			mPokeysLock;	//	for when resizing array
	Array<std::shared_ptr<TPokeyMeta>>	mPokeys;
};

class TPollPokeyThread : public SoyWorkerThread
{
public:
	TPollPokeyThread(TPokeyManager& PokeyManager,TChannelManager& Channels);

	virtual bool		Iteration() override;

	bool			IsEnabled() const { return mEnabled; }
	void			Enable(bool Enable) { mEnabled = Enable; }
	virtual std::chrono::milliseconds	GetSleepDuration();

public:
	void				SendGetDeviceMeta();
	void				SendGetUserMeta();
	void				SendGetDeviceState();
	void				SendJob(TJob& Job);
	
private:
	TPokeyManager&		mPokeyManager;
	TChannelManager&	mChannels;
	bool				mEnabled;
};


//	every N secs look for new pokeys
class TPokeyDiscoverThread : public SoyWorkerThread
{
public:
	TPokeyDiscoverThread(std::shared_ptr<TChannel>& Channel);
	
	bool			IsEnabled() const	{ return mEnabled; }
	void			Enable(bool Enable)	{ mEnabled = Enable; }
	virtual bool	Iteration() override;
	virtual std::chrono::milliseconds	GetSleepDuration()	{	return std::chrono::milliseconds(2000);	}

	std::shared_ptr<TChannel>&	mChannel;
	bool						mEnabled;
};


class TPopPokey : public TJobHandler, public TPopJobHandler, public TChannelManager, public TPokeyManager
{
public:
	TPopPokey();
	virtual ~TPopPokey();
	
	virtual bool	AddChannel(std::shared_ptr<TChannel> Channel) override;

	void			OnInitPokey(TJobAndChannel& JobAndChannel);
	void			OnSetupPokey(TJobAndChannel& JobAndChannel);
	void			OnDiscoverPokey(TJobAndChannel& JobAndChannel);
	void			OnListPokeys(TJobAndChannel& JobChannel);
	void			OnGetStatus(TJobAndChannel& JobChannel);
	void			OnExit(TJobAndChannel& JobChannel);
	void			OnPopGridCoord(TJobAndChannel& JobAndChannel);
	void			OnPeekGridCoord(TJobAndChannel& JobAndChannel);
	void			OnPushGridCoord(TJobAndChannel& JobAndChannel);
	void			OnPopLaserGateState(TJobAndChannel& JobAndChannel);
	void			OnPeekLaserGateState(TJobAndChannel& JobAndChannel);
	void			OnPushLaserGateState(TJobAndChannel& JobAndChannel);
	void			OnUnknownPokeyReply(TJobAndChannel& JobAndChannel);
	void			OnPokeyPollReply(TJobAndChannel& JobAndChannel);
	void			OnEnableDiscovery(TJobAndChannel& JobAndChannel);
	void			OnDisableDiscovery(TJobAndChannel& JobAndChannel);
	void			OnEnablePoll(TJobAndChannel& JobAndChannel);
	void			OnDisablePoll(TJobAndChannel& JobAndChannel);
	void			OnFakeDiscoverPokeys(TJobAndChannel& JobAndChannel);
	void			OnIgnorePokey(TJobAndChannel& JobAndChannel);

	void			UpdatePinState(TPokeyMeta& Pokey,const ArrayBridge<char>& Pins);
	void			PushGridCoord(vec2x<int> GridCoord);
	void			PushLaserGateState(bool State);
	bool			EnableDiscovery(bool Enable, bool& OldState);
	bool			EnablePoll(bool Enable, bool& OldState);

	void			GetConnectedStatus(std::ostream& Status);
	void			GetIgnoredPinStatus(std::ostream& Status);

public:
	Soy::Platform::TConsoleApp	mConsoleApp;

	std::shared_ptr<TPokeyDiscoverThread>	mDiscoverPokeyThread;
	std::shared_ptr<TPollPokeyThread>	mPollPokeyThread;

	std::shared_ptr<TChannel>	mDiscoverPokeyChannel;

	
	std::mutex					mLastGridCoordLock;
	vec2x<int>					mLastGridCoord;
	bool						mLaserGateState;

	SoyTime						mLastGridCoordTime;		//	time coord was last set
	SoyTime						mLastLaserGateTime;		//	time coord was last set
};



