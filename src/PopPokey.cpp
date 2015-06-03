#include "PopPokey.h"
#include <TParameters.h>
#include <SoyDebug.h>
#include <TProtocolCli.h>
#include <TProtocolHttp.h>
#include <SoyApp.h>
#include <PopMain.h>
#include <TJobRelay.h>
#include <SoyPixels.h>
#include <SoyString.h>
#include <TFeatureBinRing.h>
#include <SortArray.h>
#include <TChannelLiteral.h>
#include <RemoteArray.h>
#include <TChannelFile.h>

const char* TPokeyMeta::CoordDelim = "/";
const char* TPokeyMeta::CoordComponentDelim = ",";
const vec2x<int> TPokeyMeta::GridCoordLaserGate = vec2x<int>(-99,-99);
const vec2x<int> TPokeyMeta::GridCoordInvalid = vec2x<int>(-1,-1);
const char* TPokeyMeta::LaserGateOnReply = "lasergate_on";
const char* TPokeyMeta::LaserGateOffReply = "lasergate_off";
float TPokeyMeta::PinDownTooLong = 2.f;


std::ostream& operator<< (std::ostream &out,const TPokeyMeta &in)
{
	static bool cr = true;
	static bool addr = true;
	static bool gridmap = true;
	static bool v = true;

	out << in.mSerial << "{";
	
	if ( in.mIgnored )
	{
		out << "IGNORED;";
	}
	
	if ( cr )
	{
		//	gr@ chrome on windows thinks there's some binary in this output and won#t display inline
		//	out << in.mChannelRef;
		if ( in.mChannelRef.IsValid() )
			out << "has channel";
		else
			out << "no channel";
	}

	if ( addr )
	{
		out << "@" << in.mAddress;
		if ( in.HasBootupAddress() )
			out << "(bootup)";
		out << " " << ( in.mDhcpEnabled ? "dhcp ip" : "fixed ip" );
		out << ";";
	}
	if ( gridmap )
		out << in.GetGridMapString().substr(0, 10) << "... x" << in.GetGridMapCount();
	if ( v )
		out << " v" << in.mVersion;

	out << "}";

	return out;
}

std::ostream& operator<< (std::ostream &out,const vec2x<int> &in)
{
	out << in.x << "," << in.y;
	return out;
}


TPinMeta::TPinMeta() :
	mDownDuration	( 0 ),
	mCoord			( TPokeyMeta::GridCoordInvalid )
{
	
}

bool TPokeyMeta::SetGridMap(std::string GridMapString,std::stringstream& Error)
{
	//	repalce tab
	for ( int i = 0; i < GridMapString.length(); i++ )
		if ( GridMapString[i] == '\t' )
			GridMapString[i] = CoordDelim[0];

	Array<std::string> IndexStrings;
	Soy::StringSplitByString( GetArrayBridge(IndexStrings), GridMapString, CoordDelim, false );
	
	if ( IndexStrings.IsEmpty() && !GridMapString.empty() )
	{
		Error << "failed to split gridmap string " << GridMapString;
		return false;
	}
	
	//	convert to indexes
	for ( int i=0;	i<IndexStrings.GetSize();	i++ )
	{
		auto& IndexString = IndexStrings[i];
		
		//	special case
		if ( IndexString == "lasergate" )
		{
			auto& Pin = GetPin(i);
			Pin.mCoord = TPokeyMeta::GridCoordLaserGate;
			continue;
		}
		
		vec2x<int> Coord(-1,-1);
		BufferArray<std::string,2> Coords;
		Soy::StringSplitByString( GetArrayBridge(Coords), IndexString, CoordComponentDelim );
		if ( Coords.GetSize() != 2 )
		{
			Error << "coord " << IndexString << " not valid";
			return false;
		}
		
		//	convert
		if ( !Soy::StringToType( Coord.x, Coords[0] ) || !Soy::StringToType( Coord.y, Coords[1] ) )
		{
			Error << "failed to turn (" << Coords[0] << ") and (" << Coords[1] << ") into coords";
			return false;
		}
		
		auto& Pin = GetPin(i);
		Pin.mCoord = Coord;
	}
	
	return true;
}

namespace Soy
{
	template<typename TYPE>
	void Clamp(TYPE& Value,const TYPE& Min,const TYPE& Max)
	{
		if ( Value < Min )
			Value = Min;
		if ( Value > Max )
			Value = Max;
	}
}

vec2x<int> TPokeyMeta::UpdatePins(const ArrayBridge<bool> &Pins)
{
	//	get delta
	SoyTime Now(true);
	if ( !mLastUpdate.IsValid() )
		mLastUpdate = Now;
	float Delta = (Now.GetTime() - mLastUpdate.GetTime()) / 1000.0f;
	mLastUpdate = Now;

	//	catch errors
	Soy::Clamp( Delta, 0.f, 1.f );
	
	vec2x<int> Result = GridCoordInvalid;

	//	update each pin
	for ( int i=0;	i<Pins.GetSize();	i++ )
	{
		bool PinDown = Pins[i];
		auto& Pin = GetPin(i);
		
		//	update how long the pin has been down (or reset)
		if ( PinDown )
			Pin.mDownDuration += Delta;
		else
			Pin.mDownDuration = 0;

		//	not down, nothging else to do
		if ( !PinDown )
			continue;

		//	check if pin is being ignored
		if ( IsPinIgnored(i) )
			continue;

		//	set as result
		auto PinGridCoord = GetPinGridCoord(i);
		if ( PinGridCoord == TPokeyMeta::GridCoordInvalid )
		{
			std::Debug << "Warning: pin " << i << " down that's out of grid-map range on " << (*this) << std::endl;
			continue;
		}
		
		Result = PinGridCoord;
	}
	
	return Result;
}

bool TPokeyMeta::IsPinIgnored(size_t Pin)
{
	//	oob
	if ( Pin >= mPins.GetSize() )
		return false;
	
	auto Duration = mPins[Pin].mDownDuration;
	if ( Duration < TPokeyMeta::PinDownTooLong )
		return false;
	
	return true;
}


vec2x<int> TPokeyMeta::GetPinGridCoord(size_t Pin)
{
	//	oob
	if ( Pin >= mPins.GetSize() )
		return TPokeyMeta::GridCoordInvalid;
	
	return mPins[Pin].mCoord;
}


float TPokeyMeta::GetPinDownDuration(size_t Pin)
{
	//	oob
	if ( Pin >= mPins.GetSize() )
		return -1.0f;
	
	return mPins[Pin].mDownDuration;
}


TPinMeta& TPokeyMeta::GetPin(size_t Pin)
{
	//	if OOB, then the array hasn't been initialised (maybe no grid layout), but we still want meta for duration etc
	if ( Pin >= mPins.GetSize() )
	{
		mPins.SetSize( Pin+1 );
	}

	return mPins[Pin];
}

float TPokeyMeta::GetTimeSinceUpdate() const
{
	//	never heard from
	if ( !mLastUpdate.IsValid() )
		return -1;
	
	SoyTime Now(true);
	auto DeltaMs = Now.GetTime() - mLastUpdate.GetTime();
	return DeltaMs / 1000.f;
}

void TPokeyMeta::GetIgnoredPins(ArrayBridge<size_t>&& IgnoredPins)
{
	for ( int p=0;	p<mPins.GetSize();	p++ )
	{
		if ( !IsPinIgnored(p) )
			continue;
		
		IgnoredPins.PushBack(p);
	}
}


TPokeyDiscoverThread::TPokeyDiscoverThread(std::shared_ptr<TChannel>& Channel) :
	mChannel		( Channel ),
	SoyWorkerThread	( Soy::GetTypeName(*this), SoyWorkerWaitMode::Sleep ),
	mEnabled( true )
{
	Start();
}



bool TPokeyDiscoverThread::Iteration()
{
	if ( !mEnabled )
		return true;
	auto Channel = mChannel;
	if ( !Channel )
		return true;
	
	//	send hello world to look for new pokeys
	TJob Job;
	Job.mParams.mCommand = TPokeyCommand::ToString( TPokeyCommand::Discover );
	Job.mChannelMeta.mChannelRef = Channel->GetChannelRef();
	Channel->SendCommand( Job );
	return true;
}




TPollPokeyThread::TPollPokeyThread(TPokeyManager& PokeyManager,TChannelManager& Channels) :
	mPokeyManager	( PokeyManager ),
	mChannels		( Channels ),
	SoyWorkerThread	( "TPollPokeyThread", SoyWorkerWaitMode::Sleep ),
	mEnabled		( true )
{
	Start();
}


std::chrono::milliseconds TPollPokeyThread::GetSleepDuration()
{
	static int DurationMs = 13;
	return std::chrono::milliseconds(DurationMs);
}


bool TPollPokeyThread::Iteration()
{
	if ( !mEnabled )
		return true;
//	SendGetDeviceMeta();
//	SendGetUserMeta();
	SendGetDeviceState();
	
	return true;
}

void TPollPokeyThread::SendGetDeviceMeta()
{
	TJob Job;
	Job.mParams.mCommand = TPokeyCommand::ToString( TPokeyCommand::GetDeviceMeta );
	SendJob( Job );
}

void TPollPokeyThread::SendGetUserMeta()
{
	TJob Job;
	Job.mParams.mCommand = TPokeyCommand::ToString( TPokeyCommand::GetUserId );
	SendJob( Job );
}

void TPollPokeyThread::SendGetDeviceState()
{
	TJob Job;
	Job.mParams.mCommand = TPokeyCommand::ToString( TPokeyCommand::GetDeviceState );
	SendJob( Job );
}

void TPollPokeyThread::SendJob(TJob& Job)
{
	Array<std::shared_ptr<TPokeyMeta>> Pokeys;
	mPokeyManager.GetPokeys( GetArrayBridge(Pokeys) );

	for ( int i=0;	i<Pokeys.GetSize();	i++ )
	{
		auto pPokey = Pokeys[i];
		if ( !pPokey )
			continue;
		if ( pPokey->mIgnored )
			continue;
		auto pChannel = mChannels.GetChannel( pPokey->mChannelRef );
		if ( !pChannel )
			continue;
		auto& Channel = *pChannel;
		if ( !Channel.IsConnected() )
			continue;
		
		Job.mChannelMeta.mChannelRef = Channel.GetChannelRef();
		Channel.SendCommand( Job );
	}

}





TPopPokey::TPopPokey() :
	TJobHandler		( static_cast<TChannelManager&>(*this) ),
	mLastGridCoord	( TPokeyMeta::GridCoordInvalid )
{
	TParameterTraits InitPokeyTraits;
	InitPokeyTraits.mAssumedKeys.PushBack("ref");
	InitPokeyTraits.mAssumedKeys.PushBack("address");
	InitPokeyTraits.mRequiredKeys.PushBack("ref");
	InitPokeyTraits.mRequiredKeys.PushBack("address");
	AddJobHandler("InitPokey", InitPokeyTraits, *this, &TPopPokey::OnInitPokey );
	
	TParameterTraits SetupPokeyTraits;
	SetupPokeyTraits.mRequiredKeys.PushBack("gridmap");
	SetupPokeyTraits.mRequiredKeys.PushBack("serial");
	AddJobHandler("SetupPokey", SetupPokeyTraits, *this, &TPopPokey::OnSetupPokey);

	AddJobHandler("list", TParameterTraits(), *this, &TPopPokey::OnListPokeys);
	AddJobHandler("exit", TParameterTraits(), *this, &TPopPokey::OnExit);
	AddJobHandler("error", TParameterTraits(), *this, &TPopPokey::OnGetStatus);

	AddJobHandler("PopGridCoord", TParameterTraits(), *this, &TPopPokey::OnPopGridCoord);
	AddJobHandler("PeekGridCoord", TParameterTraits(), *this, &TPopPokey::OnPeekGridCoord);

	TParameterTraits PushGridCoordTraits;
	PushGridCoordTraits.mAssumedKeys.PushBack("pinx");
	PushGridCoordTraits.mRequiredKeys.PushBack("pinx");
	PushGridCoordTraits.mAssumedKeys.PushBack("piny");
	PushGridCoordTraits.mRequiredKeys.PushBack("piny");
	AddJobHandler("PushGridCoord", PushGridCoordTraits, *this, &TPopPokey::OnPushGridCoord );
	
	AddJobHandler("PopLaserGate", TParameterTraits(), *this, &TPopPokey::OnPopLaserGateState);
	AddJobHandler("PeekLaserGate", TParameterTraits(), *this, &TPopPokey::OnPeekLaserGateState);

	TParameterTraits PushLaserGateStateTraits;
	PushLaserGateStateTraits.mAssumedKeys.PushBack("state");
	PushLaserGateStateTraits.mRequiredKeys.PushBack("state");
	AddJobHandler("PushLaserGate", PushLaserGateStateTraits, *this, &TPopPokey::OnPushLaserGateState );
	
	AddJobHandler( TJobParams::CommandReplyPrefix + TPokeyCommand::ToString( TPokeyCommand::UnknownReply ), TParameterTraits(), *this, &TPopPokey::OnUnknownPokeyReply );
	
	AddJobHandler( TJobParams::CommandReplyPrefix + TPokeyCommand::ToString( TPokeyCommand::Discover ), TParameterTraits(), *this, &TPopPokey::OnDiscoverPokey );

	AddJobHandler( TJobParams::CommandReplyPrefix + TPokeyCommand::ToString( TPokeyCommand::GetDeviceState ), TParameterTraits(), *this, &TPopPokey::OnPokeyPollReply );
	
	mPollPokeyThread.reset( new TPollPokeyThread( *this, static_cast<TChannelManager&>(*this) ) );
	mDiscoverPokeyThread.reset( new TPokeyDiscoverThread( mDiscoverPokeyChannel ) );
	
	AddJobHandler("enablediscovery", TParameterTraits(), *this, &TPopPokey::OnEnableDiscovery);
	AddJobHandler("disablediscovery", TParameterTraits(), *this, &TPopPokey::OnDisableDiscovery);
	AddJobHandler("enablepoll", TParameterTraits(), *this, &TPopPokey::OnEnablePoll);
	AddJobHandler("disablepoll", TParameterTraits(), *this, &TPopPokey::OnDisablePoll);

	TParameterTraits FakeDiscoverTraits;
	FakeDiscoverTraits.mAssumedKeys.PushBack("count");
	AddJobHandler("fakediscover", FakeDiscoverTraits, *this, &TPopPokey::OnFakeDiscoverPokeys );

	
	TParameterTraits IgnorePokeyTraits;
	IgnorePokeyTraits.mAssumedKeys.PushBack("serial");
	IgnorePokeyTraits.mDefaultParams.PushBack( std::make_tuple("ignore","1") );
	AddJobHandler("IgnorePokey", IgnorePokeyTraits, *this, &TPopPokey::OnIgnorePokey );

}


TPopPokey::~TPopPokey()
{
	//	stop threads async
	if ( mPollPokeyThread )
		mPollPokeyThread->Stop();
	
	if ( mDiscoverPokeyThread )
		mDiscoverPokeyThread->Stop();
	
	//	kill threads
	if ( mPollPokeyThread )
	{
		mPollPokeyThread->WaitToFinish();
		mPollPokeyThread.reset();
	}
	
	if ( mDiscoverPokeyThread )
	{
		mDiscoverPokeyThread->WaitToFinish();
		mDiscoverPokeyThread.reset();
	}
	
	//	shutdown channel manager
	//	shutdown job threads...
}


bool TPopPokey::AddChannel(std::shared_ptr<TChannel> Channel)
{
	if ( !TChannelManager::AddChannel( Channel ) )
		return false;

	TJobHandler::BindToChannel( *Channel );
	return true;
}

std::shared_ptr<TPokeyMeta> TPokeyManager::GetPokey(const TPokeyMeta &Pokey)
{
	std::lock_guard<std::mutex> lock(mPokeysLock);
	for ( int i=0;	i<mPokeys.GetSize();	i++ )
	{
		auto& Match = mPokeys[i];
		if ( Pokey.mSerial == Match->mSerial )
			return Match;
		if ( Pokey.mAddress == Match->mAddress )
			return Match;
	}
	
	return nullptr;
}

std::shared_ptr<TPokeyMeta> TPokeyManager::GetPokey(SoyRef ChannelRef)
{
	std::lock_guard<std::mutex> lock(mPokeysLock);
	for ( int i=0;	i<mPokeys.GetSize();	i++ )
	{
		auto& Match = mPokeys[i];
		if ( Match->mChannelRef == ChannelRef )
			return Match;
	}
	
	return nullptr;
}


std::shared_ptr<TPokeyMeta> TPokeyManager::GetPokey(int Serial,bool Create)
{
	std::lock_guard<std::mutex> lock(mPokeysLock);
	for ( int i=0;	i<mPokeys.GetSize();	i++ )
	{
		auto& Match = mPokeys[i];
		if ( Match->mSerial == Serial )
			return Match;
	}
	
	if ( !Create )
		return nullptr;

	std::shared_ptr<TPokeyMeta> Pokey( new TPokeyMeta() );
	Pokey->mSerial = Serial;
	mPokeys.PushBack( Pokey );
	return Pokey;
}


void TPopPokey::OnPokeyPollReply(TJobAndChannel& JobAndChannel)
{
	//	find pokey this is from, we don't have a serial or any id per-device so match channel
	auto& Job = JobAndChannel.GetJob();
	auto Pokey = GetPokey( Job.mChannelMeta.mChannelRef );
	if ( !Pokey )
	{
		//	gr: this comes up if you change pokey addresses whilst running... channel ref has been overwritten?
		std::Debug << "got pokey poll reply, but didn't match pokey ref " << Job.mChannelMeta.mChannelRef << std::endl;
		return;
	}
	
	//	read pins as an array of chars
	Array<char> Pins;
	if ( !Job.mParams.GetParamAs("pins", Pins ) )
	{
		std::Debug << "failed to get pokey poll pin data for " << Job.mChannelMeta.mChannelRef << std::endl;
		std::Debug << Job.mParams << std::endl;
		return;
	}
	
	//std::Debug << "pins: " << Job.mParams.GetParamAs<std::string>("pins") << std::endl;

	UpdatePinState( *Pokey, GetArrayBridge(Pins) );
}

void TPopPokey::OnFakeDiscoverPokeys(TJobAndChannel& JobAndChannel)
{
	auto& Job = JobAndChannel.GetJob();
	int FakeCount = Job.mParams.GetParamAsWithDefault<int>("count",100);
	
	//	trigger x fake jobs
	for ( int i=0;	i<FakeCount;	i++ )
	{
		TJob NewJob;
		NewJob.mParams.AddParam<int>("serial",9900+i);
		
		//	random address
		std::stringstream Address;
		int Port = 2600+i;
		Address << (rand()%256) << '.' << (rand()%256) << '.' << (rand()%256) << '.' << (rand()%256) << ':' << Port;
		NewJob.mParams.AddParam("address", Address.str() );
		NewJob.mParams.AddParam("version", "fake" );
	
		TJobAndChannel NewJobAndChannel( NewJob, JobAndChannel.GetChannel() );
		OnDiscoverPokey( NewJobAndChannel );
	}
}

void TPopPokey::OnDiscoverPokey(TJobAndChannel& JobAndChannel)
{
	//	grab it's serial and see if it already exists
	auto& Job = JobAndChannel.GetJob();

	int Serial = Job.mParams.GetParamAsWithDefault<int>("serial", -1);
	bool DhcpEnabled = Job.mParams.GetParamAsWithDefault<int>("dhcpenabled", 0)!=0;
	auto Address = Job.mParams.GetParamAs<std::string>("address");
	auto Version = Job.mParams.GetParamAs<std::string>("version");
//	std::Debug << "discovered pokey #" << Serial << " at " << Address << " v" << Version << std::endl;
	if ( Serial == -1 )
	{
		std::Debug << "got pokey discovery with no/invalid serial; " << Job.mParams << std::endl;
		return;
	}
	
	//	get pokey with this serial
	auto Pokey = GetPokey( Serial, true );
	if ( !Pokey )
	{
		std::Debug << "failed to create/find existing pokey after discovery; " << Job.mParams << std::endl;
		return;
	}

	//	update pokey meta, and if the channel differs (new, or replaced), then replace it
	bool Changed = false;
	//	todo: kill old channel
	bool NewAddress = (Pokey->mAddress != Address);

	if ( Pokey->mVersion != Version )
	{
		Pokey->mVersion = Version;
		Changed = true;
	}

	if ( Pokey->mDhcpEnabled != DhcpEnabled )
	{
		Pokey->mDhcpEnabled = DhcpEnabled;
		Changed = true;
	}

	if ( NewAddress )
	{
		if ( !Pokey->mAddress.empty() )
			std::Debug << "Pokey " << *Pokey << " changed address: " << Address << std::endl;

		Pokey->mAddress = Address;
		Changed = true;
	}
	
	//	if the pokey has changed address, or had no channel, make a new channel
	//	we cannot currently determine if the existing channel matches the address... this job won't come from the pokey's channel
	if ( NewAddress || !Pokey->mChannelRef.IsValid() )
	{
		bool CreateChannel = true;
		
		if ( Pokey->HasBootupAddress() )
		{
			std::Debug << "skipping channel creation on pokey (bootup ip) " << *Pokey << std::endl;
			CreateChannel = false;
		}
		else if ( Pokey->mIgnored )
		{
			//	gr: commented out for now as it's a bit spammy
			//std::Debug << "skipping channel creation on pokey (ignored) " << *Pokey << std::endl;
			CreateChannel = false;
		}
		else if ( Pokey->mChannelRef.IsValid() )
		{
			std::Debug << "replacing channel on pokey " << *Pokey << std::endl;
		}
		else
		{
			std::Debug << "creating new channel on pokey " << *Pokey << std::endl;
		}
		
		if ( CreateChannel )
		{
			//	create a new pokey channel
			SoyRef ChannelRef(Soy::StreamToString(std::stringstream() << Serial).c_str());
			Pokey->mChannelRef = FindUnusedChannelRef(ChannelRef);
			Changed = true;
			
			std::shared_ptr<TChannel> PokeyChannel(new TChan<TChannelSocketTcpClient, TProtocolPokey>(Pokey->mChannelRef, Pokey->mAddress));
			AddChannel(PokeyChannel);
		}
	}
	
	if ( Changed )
		std::Debug << "Updated Pokey " << (*Pokey) << std::endl;
}

void TPopPokey::OnInitPokey(TJobAndChannel& JobAndChannel)
{
	auto& Job = JobAndChannel.GetJob();
	
	auto RefString = Job.mParams.GetParamAs<std::string>("ref");
	SoyRef Ref( RefString.c_str() );
	auto Address = Job.mParams.GetParamAs<std::string>("address");

	if ( !Ref.IsValid() )
	{
		TJobReply Reply( JobAndChannel );
		std::stringstream Error;
		Error << RefString << " not a valid ref";
		Reply.mParams.AddErrorParam(Error.str());
		
		TChannel& Channel = JobAndChannel;
		Channel.OnJobCompleted( Reply );
		return;
	}

	//	create a new pokey channel
	std::shared_ptr<TChannel> PokeyChannel( new TChan<TChannelSocketTcpClient,TProtocolPokey>( Ref, Address ) );
	AddChannel( PokeyChannel );
	
	TJobReply Reply( JobAndChannel );
	std::stringstream Debug;
	Debug << "Added new channel " + PokeyChannel->GetDescription();
	Reply.mParams.AddDefaultParam( Debug.str() );
	
	TChannel& Channel = JobAndChannel;
	Channel.OnJobCompleted( Reply );
}

void TPopPokey::OnSetupPokey(TJobAndChannel& JobAndChannel)
{
	auto& Job = JobAndChannel.GetJob();

	//	get the required params
	auto GridMap = Job.mParams.GetParamAs<std::string>("gridmap");

	//	how do we identify the pokey
	int Serial = Job.mParams.GetParamAsWithDefault<int>("serial", -1);

	if ( Serial == -1 )
	{
		TJobReply Reply(JobAndChannel);
		std::stringstream Error;
		Error << "failed to parse serial param when setting gridmap=" << GridMap;
		Reply.mParams.AddErrorParam(Error.str());

		TChannel& Channel = JobAndChannel;
		Channel.OnJobCompleted(Reply);
		return;

	}

	//	fetch pokey
	std::shared_ptr<TPokeyMeta> Pokey = GetPokey(Serial, true);
	std::stringstream Error;
	Pokey->SetGridMap(GridMap, Error);

	TJobReply Reply(JobAndChannel);

	if ( !Error.str().empty() )
		Reply.mParams.AddErrorParam(Error.str());

	std::stringstream Debug;
	Debug << "Updated pokey " << ( *Pokey ) << " with gridmap: " << Pokey->GetGridMapString();
	std::Debug << Debug.str() << std::endl;
	Reply.mParams.AddDefaultParam(Debug.str());


	TChannel& Channel = JobAndChannel;
	Channel.OnJobCompleted(Reply);
}

void TPopPokey::OnExit(TJobAndChannel& JobAndChannel)
{
	mConsoleApp.Exit();
	
	//	hail may try and send a job
	TJobReply Reply(JobAndChannel);
	
	Reply.mParams.AddDefaultParam("exiting...");
	
	TChannel& Channel = JobAndChannel;
	Channel.OnJobCompleted(Reply);
}


void TPopPokey::GetConnectedStatus(std::ostream& Status)
{
	//	list number of connected pokeys
	int PokeyCount = 0;
	int PokeyConnectedCount = 0;
	
	//	copy pokeys to reduce lock contention
	Array<std::shared_ptr<TPokeyMeta>> Pokeys;
	GetPokeys( GetArrayBridge(Pokeys) );
	
	for ( int i = 0; i < Pokeys.GetSize(); i++ )
	{
		auto& Pokey = Pokeys[i];
		if ( !Pokey )
			continue;
		if ( Pokey->mIgnored )
			continue;
		
		PokeyCount++;
		
		auto pChannel = GetChannel(Pokey->mChannelRef);
		if ( pChannel )
		{
			if ( pChannel->IsConnected() )
				PokeyConnectedCount++;
		}
	}

	Status << PokeyConnectedCount << "/" << PokeyCount << " pokeys connected" << std::endl;
}



void TPopPokey::GetIgnoredPinStatus(std::ostream& Status)
{
	//	list any pokeys with ignored pins

	Array<std::shared_ptr<TPokeyMeta>> Pokeys;
	GetPokeys( GetArrayBridge(Pokeys) );
	
	for ( int p = 0; p < Pokeys.GetSize(); p++ )
	{
		auto& pPokey = Pokeys[p];
		if ( !pPokey )
			continue;
		auto& Pokey = *pPokey;
		if ( Pokey.mIgnored )
			continue;
		
		//	get list of ignored pins
		BufferArray<size_t,100> IgnoredPins;
		Pokey.GetIgnoredPins( GetArrayBridge(IgnoredPins) );
		
		if ( IgnoredPins.IsEmpty() )
			continue;
		
		Status << Pokey << " ignoring pins ";
		for ( int ipi=0;	ipi<IgnoredPins.GetSize();	ipi++ )
		{
			auto Pin = IgnoredPins[ipi];
			//	gr: display pin indexes from 1
			Status << (Pin+1) << "(" << Pokey.GetPinGridCoord(Pin) << "; " << Pokey.GetPinDownDuration(Pin) << "secs) ";
		}
		Status << std::endl;
	}
}



void TPopPokey::OnGetStatus(TJobAndChannel& JobAndChannel)
{
	//	make up a status string
	std::stringstream Status;
	
	GetConnectedStatus( Status );
	GetIgnoredPinStatus( Status );
	
	TJobReply Reply(JobAndChannel);
	Reply.mParams.AddDefaultParam( Status.str() );
	
	TChannel& Channel = JobAndChannel;
	Channel.OnJobCompleted(Reply);
}


void TPopPokey::OnListPokeys(TJobAndChannel& JobAndChannel)
{
	TJobReply Reply(JobAndChannel);

	std::stringstream ReplyString;
	Array<std::shared_ptr<TPokeyMeta>> Pokeys;
	GetPokeys( GetArrayBridge(Pokeys) );

	for ( int i = 0; i < Pokeys.GetSize(); i++ )
	{
		//	get channel state
		auto& pPokey = Pokeys[i];
		if ( !pPokey )
			continue;
		auto& Pokey = *pPokey;
		
		auto pChannel = GetChannel(Pokey.mChannelRef);
		std::string ConnectionStatus;
		if ( !pChannel )
			ConnectionStatus = "never connected";
		else if ( !pChannel->IsConnected() )
			ConnectionStatus = "disconnected";
		else
			ConnectionStatus = "connected";

		auto TimeSinceUpdate = Pokey.GetTimeSinceUpdate();
		
		ReplyString << Pokey << " " << ConnectionStatus;
		
		if ( TimeSinceUpdate >= 0.f )
			ReplyString << " (" << TimeSinceUpdate << "s ago)";

		ReplyString << std::endl;
	}

	
	Reply.mParams.AddDefaultParam(ReplyString.str());

	TChannel& Channel = JobAndChannel;
	Channel.OnJobCompleted(Reply);
}


void TPopPokey::OnEnableDiscovery(TJobAndChannel& JobAndChannel)
{
	TJobReply Reply(JobAndChannel);

	bool OldState;
	bool NewState = EnableDiscovery(true, OldState);

	std::stringstream ReplyString;
	ReplyString << "broadcast now " << ( NewState ? "enabled" : "disabled" ) << ", was " << ( OldState ? "enabled" : "disabled" );
	std::Debug << ReplyString.str() << std::endl;

	Reply.mParams.AddDefaultParam(ReplyString.str());

	TChannel& Channel = JobAndChannel;
	Channel.OnJobCompleted(Reply);
}

void TPopPokey::OnDisableDiscovery(TJobAndChannel& JobAndChannel)
{
	TJobReply Reply(JobAndChannel);

	bool OldState;
	bool NewState = EnableDiscovery(false, OldState);

	std::stringstream ReplyString;
	ReplyString << "broadcast now " << ( NewState ? "enabled" : "disabled" ) << ", was " << ( OldState ? "enabled" : "disabled" );
	std::Debug << ReplyString.str() << std::endl;

	Reply.mParams.AddDefaultParam(ReplyString.str());

	TChannel& Channel = JobAndChannel;
	Channel.OnJobCompleted(Reply);
}

void TPopPokey::OnEnablePoll(TJobAndChannel& JobAndChannel)
{
	TJobReply Reply(JobAndChannel);

	bool OldState;
	bool NewState = EnablePoll(true, OldState);

	std::stringstream ReplyString;
	ReplyString << "pokey poll now " << ( NewState ? "enabled" : "disabled" ) << ", was " << ( OldState ? "enabled" : "disabled" );
	std::Debug << ReplyString.str() << std::endl;

	Reply.mParams.AddDefaultParam(ReplyString.str());

	TChannel& Channel = JobAndChannel;
	Channel.OnJobCompleted(Reply);
}

void TPopPokey::OnDisablePoll(TJobAndChannel& JobAndChannel)
{
	TJobReply Reply(JobAndChannel);

	bool OldState;
	bool NewState = EnablePoll(false, OldState);

	std::stringstream ReplyString;
	ReplyString << "pokey poll now " << ( NewState ? "enabled" : "disabled" ) << ", was " << ( OldState ? "enabled" : "disabled" );
	std::Debug << ReplyString.str() << std::endl;

	Reply.mParams.AddDefaultParam(ReplyString.str());

	TChannel& Channel = JobAndChannel;
	Channel.OnJobCompleted(Reply);
}


void TPopPokey::OnPushGridCoord(TJobAndChannel& JobAndChannel)
{
	auto& Job = JobAndChannel.GetJob();
	
	vec2x<int> GridCoord;
	GridCoord.x = Job.mParams.GetParamAsWithDefault<int>("pinx", -1);
	GridCoord.y = Job.mParams.GetParamAsWithDefault<int>("piny", -1);
	PushGridCoord( GridCoord );
	
	TJobReply Reply( JobAndChannel );
	std::stringstream Debug;
	Debug << "Set grid coord to " << GridCoord;
	Reply.mParams.AddDefaultParam( Debug.str() );
	
	TChannel& Channel = JobAndChannel;
	Channel.OnJobCompleted( Reply );
}

void TPopPokey::OnPopGridCoord(TJobAndChannel& JobAndChannel)
{
	auto& Job = JobAndChannel.GetJob();
	
	mLastGridCoordLock.lock();
	auto LastGridCoord = mLastGridCoord;
	mLastGridCoord = TPokeyMeta::GridCoordInvalid;
	mLastGridCoordLock.unlock();
	
	TJobReply Reply( JobAndChannel );
	std::stringstream ReplyString;
	if ( LastGridCoord == TPokeyMeta::GridCoordLaserGate )
		ReplyString << "lasergate";
	else
		ReplyString << LastGridCoord;
	Reply.mParams.AddDefaultParam( ReplyString.str() );
	
	TChannel& Channel = JobAndChannel;
	Channel.OnJobCompleted( Reply );
}


void TPopPokey::OnPeekGridCoord(TJobAndChannel& JobAndChannel)
{
	auto& Job = JobAndChannel.GetJob();

	auto LastGridCoord = mLastGridCoord;
	//	if its been X secs since coord was changed, then return invalid
	auto TimeDiff = SoyTime(true).GetTime() - mLastGridCoordTime.GetTime();
	if ( TimeDiff > 1000 )
		LastGridCoord = TPokeyMeta::GridCoordInvalid;

	TJobReply Reply(JobAndChannel);
	std::stringstream ReplyString;
	if ( LastGridCoord == TPokeyMeta::GridCoordLaserGate )
		ReplyString << "lasergate";
	else
		ReplyString << LastGridCoord;
	Reply.mParams.AddDefaultParam(ReplyString.str());

	TChannel& Channel = JobAndChannel;
	Channel.OnJobCompleted(Reply);
}

void TPopPokey::OnPushLaserGateState(TJobAndChannel& JobAndChannel)
{
	auto& Job = JobAndChannel.GetJob();
	
	bool State = false;
	State = Job.mParams.GetParamAsWithDefault<int>("state", 0) != 0;
	PushLaserGateState( State );
	
	TJobReply Reply( JobAndChannel );
	std::stringstream Debug;
	Debug << "Set laser gate state to " << State;
	Reply.mParams.AddDefaultParam( Debug.str() );
	
	TChannel& Channel = JobAndChannel;
	Channel.OnJobCompleted( Reply );
}

void TPopPokey::OnPopLaserGateState(TJobAndChannel& JobAndChannel)
{
	auto& Job = JobAndChannel.GetJob();

	mLastGridCoordLock.lock();
	auto LastState = mLaserGateState;
	mLaserGateState = false;
	mLastGridCoordLock.unlock();

	TJobReply Reply(JobAndChannel);
	std::stringstream ReplyString;
	if ( LastState )
		ReplyString << "lasergate_on";
	else
		ReplyString << "lasergate_off";
	Reply.mParams.AddDefaultParam(ReplyString.str());

	TChannel& Channel = JobAndChannel;
	Channel.OnJobCompleted(Reply);
}


void TPopPokey::OnPeekLaserGateState(TJobAndChannel& JobAndChannel)
{
	auto& Job = JobAndChannel.GetJob();

	auto LastState = mLaserGateState;
	//	if its been X secs since coord was changed, then return invalid
	auto TimeDiff = SoyTime(true).GetTime() - mLastLaserGateTime.GetTime();
	if ( TimeDiff > 1000 )
		LastState = false;

	TJobReply Reply(JobAndChannel);
	std::stringstream ReplyString;
	if ( LastState )
		ReplyString << "lasergate_on";
	else
		ReplyString << "lasergate_off";
	Reply.mParams.AddDefaultParam(ReplyString.str());

	TChannel& Channel = JobAndChannel;
	Channel.OnJobCompleted(Reply);
}


void TPopPokey::OnIgnorePokey(TJobAndChannel& JobAndChannel)
{
	auto& Job = JobAndChannel.GetJob();
	
	auto Serial = Job.mParams.GetParamAs<int>("serial");
	auto NewIgnore = Job.mParams.GetParamAsWithDefault<int>("ignore",true);
	
	//	fetch pokey
	std::shared_ptr<TPokeyMeta> Pokey = GetPokey(Serial, true);
	
	auto OldIgnore = Pokey->mIgnored;
	Pokey->mIgnored = NewIgnore;

	TJobReply Reply(JobAndChannel);
	std::stringstream ReplyString;
	ReplyString << "Updated Pokey " << ( *Pokey ) << ", " << (NewIgnore ? "IGNORED" : "NOT ignored" ) << ", was " << ( OldIgnore ? "IGNORED" : "NOT ignored" ) << std::endl;
	
	Reply.mParams.AddDefaultParam(ReplyString.str());
	
	
	TChannel& Channel = JobAndChannel;
	Channel.OnJobCompleted(Reply);
}

void TPopPokey::OnUnknownPokeyReply(TJobAndChannel& JobAndChannel)
{
	auto& Job = JobAndChannel.GetJob();
	TChannel& Channel = JobAndChannel;
	
	std::Debug << "got pokey reply from " << Channel.GetChannelRef() << ": RequestId #" << Job.mParams.GetParamAs<int>("requestid") << std::endl;
	std::Debug << Job.mParams << std::endl;
}

bool TPopPokey::EnableDiscovery(bool Enable, bool& OldState)
{
	if ( !mDiscoverPokeyThread )
	{
		OldState = false;
		return false;
	}

	OldState = mDiscoverPokeyThread->IsEnabled();
	mDiscoverPokeyThread->Enable(Enable);
	return mDiscoverPokeyThread->IsEnabled();
}

bool TPopPokey::EnablePoll(bool Enable, bool& OldState)
{
	if ( !mPollPokeyThread )
	{
		OldState = false;
		return false;
	}

	OldState = mPollPokeyThread->IsEnabled();
	mPollPokeyThread->Enable(Enable);
	return mPollPokeyThread->IsEnabled();
}


void TPopPokey::UpdatePinState(TPokeyMeta& Pokey,const ArrayBridge<char>& Pins)
{
	//	convert pin chars to bools
	Array<bool> PinBools;
	for ( int i=0;	i<Pins.GetSize();	i++ )
	{
		bool PinDown = (Pins[i] != '0');
		PinBools.PushBack(PinDown);
	}
	
	auto GridDown = Pokey.UpdatePins( GetArrayBridge(PinBools) );
	if ( GridDown != TPokeyMeta::GridCoordInvalid )
	{
		PushGridCoord( GridDown );
	}
	
}


void TPopPokey::PushGridCoord(vec2x<int> GridCoord)
{
	//	if laser gate, set the state
	if ( GridCoord == TPokeyMeta::GridCoordLaserGate )
	{
		PushLaserGateState(true);
		return;
	}
	
	mLastGridCoordLock.lock();
	mLastGridCoord = GridCoord;
	mLastGridCoordTime = SoyTime(true);
	mLastGridCoordLock.unlock();
	
	std::Debug << "pin set to " << GridCoord << std::endl;
}


void TPopPokey::PushLaserGateState(bool State)
{
	mLastGridCoordLock.lock();
	mLaserGateState = State;
	mLastLaserGateTime = SoyTime(true);
	mLastGridCoordLock.unlock();
	
	std::Debug << "laser gate set to " << State << std::endl;
}





//	horrible global for lambda
std::shared_ptr<TChannel> gStdioChannel;
std::shared_ptr<TChannel> gCaptureChannel;



TPopAppError::Type PopMain(TJobParams& Params)
{
	TPopPokey App;

	auto CommandLineChannel = std::shared_ptr<TChan<TChannelLiteral,TProtocolCli>>( new TChan<TChannelLiteral,TProtocolCli>( SoyRef("cmdline") ) );
	
	//	create stdio channel for commandline output
	gStdioChannel = CreateChannelFromInputString("std:", SoyRef("stdio") );

	
	App.mDiscoverPokeyChannel.reset( new TChan<TChannelSocketUdpBroadcastClient,TProtocolPokey>( SoyRef("discover"), 20055 ) );

	auto HttpChannel = CreateChannelFromInputString("http:8080",SoyRef("http"));

	
	App.AddChannel( CommandLineChannel );
	App.AddChannel( App.mDiscoverPokeyChannel );
	App.AddChannel( gStdioChannel );
	App.AddChannel( HttpChannel );

	
	//	when the commandline SENDs a command (a reply), send it to stdout
	auto RelayFunc = [](TJobAndChannel& JobAndChannel)
	{
		if ( !gStdioChannel )
			return;
		TJob Job = JobAndChannel;
		Job.mChannelMeta.mChannelRef = gStdioChannel->GetChannelRef();
		Job.mChannelMeta.mClientRef = SoyRef();
		gStdioChannel->SendCommand( Job );
	};
	
	CommandLineChannel->mOnJobSent.AddListener( RelayFunc );
	CommandLineChannel->mOnJobRecieved.AddListener( RelayFunc );

	
	
	
	//	bootup commands via a channel
	std::string ConfigFilename = Params.GetParamAs<std::string>("config");
	if ( ConfigFilename.empty() )
		ConfigFilename = "bootup.txt";
	std::shared_ptr<TChannel> BootupChannel( new TChan<TChannelFileRead,TProtocolCli>( SoyRef("Bootup"), ConfigFilename ) );

	//	display reply to stdout
	BootupChannel->mOnJobRecieved.AddListener( RelayFunc );
	
	App.AddChannel( BootupChannel );
	
	
	//	run
	App.mConsoleApp.WaitForExit();

	gStdioChannel.reset();
	return TPopAppError::Success;
}




