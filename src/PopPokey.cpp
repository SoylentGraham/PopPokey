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



std::ostream& operator<< (std::ostream &out,const TPokeyMeta &in)
{
	out << in.mSerial << "{" << in.mChannelRef << "@" << in.mAddress << ";" << in.GetGridMapString() << "}";
	return out;
}


bool TPokeyMeta::SetGridMap(const std::string& GridMapString,std::stringstream& Error)
{
	mPinToGridMap.Clear();
	
	Array<std::string> IndexStrings;
	Soy::StringSplitByString( GetArrayBridge(IndexStrings), GridMapString, "," );
	
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
		int Index = -1;
		
		if ( !Soy::StringToType( Index, IndexString ) || Index < 0 )
		{
			Error << "failed to turn " << IndexString << " into index";
			return false;
		}
		
		mPinToGridMap.PushBack( Index );
	}
	
	return true;
}

	
TPokeyDiscoverThread::TPokeyDiscoverThread(std::shared_ptr<TChannel>& Channel) :
	mChannel		( Channel ),
	SoyWorkerThread	( Soy::GetTypeName(*this), SoyWorkerWaitMode::Sleep )
{
	Start();
}

bool TPokeyDiscoverThread::Iteration()
{
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
	SoyWorkerThread	( "TPollPokeyThread", SoyWorkerWaitMode::Sleep )
{
	Start();
}


bool TPollPokeyThread::Iteration()
{
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
	TJobHandler	( static_cast<TChannelManager&>(*this) )
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
	AddJobHandler("SetupPokey", SetupPokeyTraits, *this, &TPopPokey::OnSetupPokey );
	
	TParameterTraits PopGridPinTraits;
	AddJobHandler("PopGridPin", PopGridPinTraits, *this, &TPopPokey::OnPopGridPin );
	
	TParameterTraits PushGridPinTraits;
	PushGridPinTraits.mAssumedKeys.PushBack("pin");
	PushGridPinTraits.mRequiredKeys.PushBack("pin");
	AddJobHandler("PushGridPin", PushGridPinTraits, *this, &TPopPokey::OnPushGridPin );
	
	AddJobHandler( TJobParams::CommandReplyPrefix + TPokeyCommand::ToString( TPokeyCommand::UnknownReply ), TParameterTraits(), *this, &TPopPokey::OnUnknownPokeyReply );
	
	AddJobHandler( TJobParams::CommandReplyPrefix + TPokeyCommand::ToString( TPokeyCommand::Discover ), TParameterTraits(), *this, &TPopPokey::OnDiscoverPokey );

	AddJobHandler( TJobParams::CommandReplyPrefix + TPokeyCommand::ToString( TPokeyCommand::GetDeviceState ), TParameterTraits(), *this, &TPopPokey::OnPokeyPollReply );
	
	mPollPokeyThread.reset( new TPollPokeyThread( static_cast<TChannelManager&>(*this) ) );
	mDiscoverPokeyThread.reset( new TPokeyDiscoverThread( mDiscoverPokeyChannel ) );
	
}

void TPopPokey::AddChannel(std::shared_ptr<TChannel> Channel)
{
	TChannelManager::AddChannel( Channel );
	if ( !Channel )
		return;
	TJobHandler::BindToChannel( *Channel );
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


void TPopPokey::OnDiscoverPokey(TJobAndChannel& JobAndChannel)
{
	//	grab it's serial and see if it already exists
	auto& Job = JobAndChannel.GetJob();

	int Serial = Job.mParams.GetParamAsWithDefault<int>("serial",-1);
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
	auto Address = Job.mParams.GetParamAs<std::string>("address");
	bool NewAddress = (Pokey->mAddress != Address);

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
		if ( Pokey->mChannelRef.IsValid() )
			std::Debug << "replacing channel on pokey " << *Pokey << std::endl;
		else
			std::Debug << "creating new channel on pokey " << *Pokey << std::endl;
		
		//	create a new pokey channel
		SoyRef ChannelRef( Soy::StreamToString( std::stringstream() << Serial ).c_str() );
		Pokey->mChannelRef = FindUnusedChannelRef( ChannelRef );
		Changed = true;
		std::shared_ptr<TChannel> PokeyChannel( new TChan<TChannelSocketTcpClient,TProtocolPokey>( Pokey->mChannelRef, Pokey->mAddress ) );
		AddChannel( PokeyChannel );
		if ( mPollPokeyThread )
			mPollPokeyThread->AddPokeyChannel( PokeyChannel->GetChannelRef() );
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
	int Serial = Job.mParams.GetParamAsWithDefault<int>("serial",-1);

	if ( Serial == -1 )
	{
		TJobReply Reply( JobAndChannel );
		std::stringstream Error;
		Error << "failed to parse serial param when setting gridmap=" << GridMap;
		Reply.mParams.AddErrorParam(Error.str());
		
		TChannel& Channel = JobAndChannel;
		Channel.OnJobCompleted( Reply );
		return;

	}

	//	fetch pokey
	std::shared_ptr<TPokeyMeta> Pokey = GetPokey( Serial, true );
	std::stringstream Error;
	Pokey->SetGridMap( GridMap, Error );

	TJobReply Reply( JobAndChannel );
	std::stringstream Debug;
	Debug << "Updated pokey " << (*Pokey) << " with gridmap: " << Pokey->GetGridMapString();
	std::Debug << Debug.str() << std::endl;
	Reply.mParams.AddDefaultParam( Debug.str() );
	
	TChannel& Channel = JobAndChannel;
	Channel.OnJobCompleted( Reply );
}


void TPopPokey::OnPushGridPin(TJobAndChannel& JobAndChannel)
{
	auto& Job = JobAndChannel.GetJob();
	
	auto GridPin = Job.mParams.GetParamAsWithDefault<int>("pin", -1);
	PushGridPin( GridPin );
	
	TJobReply Reply( JobAndChannel );
	std::stringstream Debug;
	Debug << "Set pin to " << GridPin;
	Reply.mParams.AddDefaultParam( Debug.str() );
	
	TChannel& Channel = JobAndChannel;
	Channel.OnJobCompleted( Reply );
}

void TPopPokey::OnPopGridPin(TJobAndChannel& JobAndChannel)
{
	auto& Job = JobAndChannel.GetJob();
	
	mLastGridPinLock.lock();
	int LastGridPin = mLastGridPin;
	mLastGridPin = -1;
	mLastGridPinLock.unlock();
	
	TJobReply Reply( JobAndChannel );
	std::stringstream Debug;
	Debug << LastGridPin;
	Reply.mParams.AddDefaultParam( Debug.str() );
	
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

		PushGridPin( Pokey.mPinToGridMap[i] );
	}
}


void TPopPokey::PushGridPin(int GridPin)
{
	mLastGridPinLock.lock();
	mLastGridPin = GridPin;
	mLastGridPinLock.unlock();
	
	std::Debug << "pin set to " << GridPin << std::endl;
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
	
	App.AddChannel( CommandLineChannel );
	App.AddChannel( App.mDiscoverPokeyChannel );
	App.AddChannel( gStdioChannel );

	
	//	bootup commands
	std::string ConfigFilename = Params.GetParamAs<std::string>("config");
	if ( ConfigFilename.empty() )
		ConfigFilename = "bootup.txt";
	
	Array<std::string> Commands;

	//	parse command file
	std::stringstream ConfigFileError;
	Soy::FileToStringLines( ConfigFilename, GetArrayBridge(Commands), ConfigFileError );
	if ( !ConfigFileError.str().empty() )
		std::Debug << "config file " << ConfigFilename << " error: " << ConfigFileError.str() << std::endl;

	//	hard coded init commands
	Commands.PushBack("setuppokey serial=21244 gridmap=100,101,102,103,104,105,106,107,108");
	Commands.PushBack("setuppokey serial=22961 gridmap=1,2,3,4,5,6,7,8,9,10");

	
	//Commands.PushBack("initpokey hello 10.0.0.54:20055\n");

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
	}

	
	
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
	
	
	
	
	//	run
	App.mConsoleApp.WaitForExit();

	gStdioChannel.reset();
	return TPopAppError::Success;
}




