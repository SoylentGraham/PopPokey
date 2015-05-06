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


TPollPokeyThread::TPollPokeyThread(TChannelManager& Channels) :
	mChannels		( Channels ),
	SoyWorkerThread	( "TPollPokeyThread", SoyWorkerWaitMode::Sleep )
{
	Start();
}


bool TPollPokeyThread::Iteration()
{
	//	refresh pokeys
	TJob GetDigitalPins;
	GetDigitalPins.mParams.mCommand = "GetDeviceReport0xCC";
	
	for ( int i=0;	i<mPokeyChannels.GetSize();	i++ )
	{
		auto pChannel = mChannels.GetChannel( mPokeyChannels[i] );
		if ( !pChannel )
			continue;
		auto& Channel = *pChannel;
		if ( !Channel.IsConnected() )
			continue;
		
		GetDigitalPins.mChannelMeta.mChannelRef = Channel.GetChannelRef();
		Channel.SendCommand( GetDigitalPins );
	}

	return true;
}




TDecodeResult::Type TProtocolPokey::DecodeHeader(TJob& Job,TChannelStream& Stream)
{
	return TDecodeResult::Error;
}

TDecodeResult::Type TProtocolPokey::DecodeData(TJob& Job,TChannelStream& Stream)
{
	return TDecodeResult::Error;
}
	
bool TProtocolPokey::Encode(const TJobReply& Reply,std::stringstream& Output)
{
	return false;
}

bool TProtocolPokey::Encode(const TJobReply& Reply,Array<char>& Output)
{
	return false;
}

bool TProtocolPokey::Encode(const TJob& Job,std::stringstream& Output)
{
	return false;
}

bool TProtocolPokey::Encode(const TJob& Job,Array<char>& Output)
{
	return false;
}
	
bool TProtocolPokey::FixParamFormat(TJobParam& Param,std::stringstream& Error)
{
	return true;
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

	TParameterTraits PopGridEventTraits;
	AddJobHandler("PopGridEvent", PopGridEventTraits, *this, &TPopPokey::OnPopGridEvent );
	
	mPollPokeyThread.reset( new TPollPokeyThread( static_cast<TChannelManager&>(*this) ) );
}

void TPopPokey::AddChannel(std::shared_ptr<TChannel> Channel)
{
	TChannelManager::AddChannel( Channel );
	if ( !Channel )
		return;
	TJobHandler::BindToChannel( *Channel );
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


void TPopPokey::OnPopGridEvent(TJobAndChannel& JobAndChannel)
{
	auto& Job = JobAndChannel.GetJob();
	
	TJobReply Reply( JobAndChannel );
	std::stringstream Debug;
	Debug << "-1";
	Reply.mParams.AddDefaultParam( Debug.str() );
	
	TChannel& Channel = JobAndChannel;
	Channel.OnJobCompleted( Reply );
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
	
	
	App.AddChannel( CommandLineChannel );
	App.AddChannel( gStdioChannel );

	
	//	bootup commands
	//std::string ConfigFilename = Params.GetParamAs<std::string>("config");
	//if ( ConfigFilename.empty() )
	//	ConfigFilename = "bootup.txt";
	
	Array<std::string> Commands;
	Commands.PushBack("initpokey hello 10.0.0.54:20055\n");
	//ParseConfig( ConfigFilename, GetArrayBridge( Commands ) );
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




