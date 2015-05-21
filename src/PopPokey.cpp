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


const char* TPokeyMeta::CoordDelim = "/";
const char* TPokeyMeta::CoordComponentDelim = ",";
const vec2x<int> TPokeyMeta::GridCoordLaserGate = vec2x<int>(-99,-99);
const vec2x<int> TPokeyMeta::GridCoordInvalid = vec2x<int>(-1,-1);
const char* TPokeyMeta::LaserGateOnReply = "lasergate_on";
const char* TPokeyMeta::LaserGateOffReply = "lasergate_off";


std::ostream& operator<< (std::ostream &out,const TPokeyMeta &in)
{
	static bool cr = true;
	static bool addr = true;
	static bool gridmap = true;
	static bool v = true;

	out << in.mSerial << "{";
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


bool TPokeyMeta::SetGridMap(std::string GridMapString,std::stringstream& Error)
{
	mPinToGridMap.Clear();
	
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
		if ( mPinToGridMap.IsFull() )
		{
			Error << "grid map indexes full (" << mPinToGridMap.GetSize() << ")";
			return false;
		}
		
		auto& IndexString = IndexStrings[i];
		
		//	special case
		if ( IndexString == "lasergate" )
		{
			mPinToGridMap.PushBack( TPokeyMeta::GridCoordLaserGate );
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
		
		mPinToGridMap.PushBack( Coord );
	}
	
	return true;
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




TPollPokeyThread::TPollPokeyThread(TChannelManager& Channels) :
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
	for ( int i=0;	i<mPokeyChannels.GetSize();	i++ )
	{
		auto pChannel = mChannels.GetChannel( mPokeyChannels[i] );
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

	TParameterTraits PopGridCoordTraits;
	AddJobHandler("PopGridCoord", PopGridCoordTraits, *this, &TPopPokey::OnPopGridCoord );
	
	TParameterTraits PushGridCoordTraits;
	PushGridCoordTraits.mAssumedKeys.PushBack("pinx");
	PushGridCoordTraits.mRequiredKeys.PushBack("pinx");
	PushGridCoordTraits.mAssumedKeys.PushBack("piny");
	PushGridCoordTraits.mRequiredKeys.PushBack("piny");
	AddJobHandler("PushGridCoord", PushGridCoordTraits, *this, &TPopPokey::OnPushGridCoord );
	
	TParameterTraits PopLaserGateStateTraits;
	AddJobHandler("PopLaserGate", PopLaserGateStateTraits, *this, &TPopPokey::OnPopLaserGateState );
	
	TParameterTraits PushLaserGateStateTraits;
	PushLaserGateStateTraits.mAssumedKeys.PushBack("state");
	PushLaserGateStateTraits.mRequiredKeys.PushBack("state");
	AddJobHandler("PushLaserGate", PushLaserGateStateTraits, *this, &TPopPokey::OnPushLaserGateState );
	
	AddJobHandler( TJobParams::CommandReplyPrefix + TPokeyCommand::ToString( TPokeyCommand::UnknownReply ), TParameterTraits(), *this, &TPopPokey::OnUnknownPokeyReply );
	
	AddJobHandler( TJobParams::CommandReplyPrefix + TPokeyCommand::ToString( TPokeyCommand::Discover ), TParameterTraits(), *this, &TPopPokey::OnDiscoverPokey );

	AddJobHandler( TJobParams::CommandReplyPrefix + TPokeyCommand::ToString( TPokeyCommand::GetDeviceState ), TParameterTraits(), *this, &TPopPokey::OnPokeyPollReply );
	
	mPollPokeyThread.reset( new TPollPokeyThread( static_cast<TChannelManager&>(*this) ) );
	mDiscoverPokeyThread.reset( new TPokeyDiscoverThread( mDiscoverPokeyChannel ) );
	
	AddJobHandler("enablediscovery", TParameterTraits(), *this, &TPopPokey::OnEnableDiscovery);
	AddJobHandler("disablediscovery", TParameterTraits(), *this, &TPopPokey::OnDisableDiscovery);
	AddJobHandler("enablepoll", TParameterTraits(), *this, &TPopPokey::OnEnablePoll);
	AddJobHandler("disablepoll", TParameterTraits(), *this, &TPopPokey::OnDisablePoll);

	TParameterTraits FakeDiscoverTraits;
	FakeDiscoverTraits.mAssumedKeys.PushBack("count");
	AddJobHandler("fakediscover", FakeDiscoverTraits, *this, &TPopPokey::OnFakeDiscoverPokeys );

}

bool TPopPokey::AddChannel(std::shared_ptr<TChannel> Channel)
{
	if ( !TChannelManager::AddChannel( Channel ) )
		return false;

	TJobHandler::BindToChannel( *Channel );
	return true;
}

std::shared_ptr<TPokeyMeta> TPopPokey::GetPokey(const TPokeyMeta &Pokey)
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

std::shared_ptr<TPokeyMeta> TPopPokey::GetPokey(SoyRef ChannelRef)
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


std::shared_ptr<TPokeyMeta> TPopPokey::GetPokey(int Serial,bool Create)
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
		if ( !Pokey->HasBootupAddress() )
			std::Debug << "skipping channel creation on pokey " << *Pokey << std::endl;
		else if ( Pokey->mChannelRef.IsValid() )
			std::Debug << "replacing channel on pokey " << *Pokey << std::endl;
		else
			std::Debug << "creating new channel on pokey " << *Pokey << std::endl;
		
		if ( !Pokey->HasBootupAddress() )
		{
			//	create a new pokey channel
			SoyRef ChannelRef(Soy::StreamToString(std::stringstream() << Serial).c_str());
			Pokey->mChannelRef = FindUnusedChannelRef(ChannelRef);
			Changed = true;
			
			std::shared_ptr<TChannel> PokeyChannel(new TChan<TChannelSocketTcpClient, TProtocolPokey>(Pokey->mChannelRef, Pokey->mAddress));
			AddChannel(PokeyChannel);
			if ( mPollPokeyThread )
				mPollPokeyThread->AddPokeyChannel(PokeyChannel->GetChannelRef());
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
	mPollPokeyThread->AddPokeyChannel( PokeyChannel->GetChannelRef() );
	
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
void TPopPokey::OnListPokeys(TJobAndChannel& JobAndChannel)
{
	TJobReply Reply(JobAndChannel);

	std::stringstream ReplyString;
	mPokeysLock.lock();
	for ( int i = 0; i < mPokeys.GetSize(); i++ )
		ReplyString << *mPokeys[i] << std::endl;
	mPokeysLock.unlock();

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
	
	TJobReply Reply( JobAndChannel );
	std::stringstream ReplyString;
	if ( LastState )
		ReplyString << "lasergate_on";
	else
		ReplyString << "lasergate_off";
	Reply.mParams.AddDefaultParam( ReplyString.str() );
	
	TChannel& Channel = JobAndChannel;
	Channel.OnJobCompleted( Reply );
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
	for ( int i=0;	i<Pins.GetSize();	i++ )
	{
		bool PinDown = (Pins[i]!='0');
		
		if ( !PinDown )
			continue;

		//	turn pin to grid index
		if ( i >= Pokey.mPinToGridMap.GetSize() )
		{
			std::Debug << "Warning: pin " << i << " down that's out of grid-map range on " << Pokey << std::endl;
			continue;
		}

		PushGridCoord( Pokey.mPinToGridMap[i] );
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
	mLastGridCoordLock.unlock();
	
	std::Debug << "pin set to " << GridCoord << std::endl;
}


void TPopPokey::PushLaserGateState(bool State)
{
	mLastGridCoordLock.lock();
	mLaserGateState = State;
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

	
	
	
	//	bootup commands
	std::string ConfigFilename = Params.GetParamAs<std::string>("config");
	if ( ConfigFilename.empty() )
		ConfigFilename = "bootup.txt";
	
	Array<std::string> Commands;

	//	parse command file
	std::stringstream ConfigFileError;

	if (!Soy::FileToStringLines(ConfigFilename, GetArrayBridge(Commands), ConfigFileError))
	{
		std::Debug << "failed to load " << ConfigFilename << "... using debug init commands" << std::endl;
		Commands.PushBack("setuppokey serial=21244 gridmap=0,0/1,0/2,0");
		Commands.PushBack("setuppokey serial=22961 gridmap=0,1/1,1/2,1");
		Commands.PushBack("setuppokey serial=22962 gridmap=lasergate");
	}

	if ( !ConfigFileError.str().empty() )
		std::Debug << "config file " << ConfigFilename << " error: " << ConfigFileError.str() << std::endl;

	for ( int i=0;	i<Commands.GetSize();	i++ )
	{
		auto Command = Commands[i];
		TProtocolCli Protocol;
		TJob Job;
		if ( !Protocol.DecodeHeader( Job, Command ) )
		{
			std::Debug << "Couldn't decode config command: " << Command << std::endl;
			continue;
		}
		CommandLineChannel->Execute( Job.mParams.mCommand, Job.mParams );
		CommandLineChannel->mOnJobRecieved.AddListener( RelayFunc );
	}

	
	
	
	
	
	
	//	run
	App.mConsoleApp.WaitForExit();

	gStdioChannel.reset();
	return TPopAppError::Success;
}




