# GPGNet4TA

[![Build Status](https://travis-ci.org/ta-forever/gpgnet4ta.svg?branch=develop)](https://travis-ci.org/ta-forever/gpgnet4ta)
[![Coverage Status](https://coveralls.io/repos/github/ta-forever/gpgnet4ta/badge.svg?branch=develop)](https://coveralls.io/github/ta-forever/gpgnet4ta?branch=develop)

An interface between GPGNet protocol and Total Annihilation with tunnelling all game traffic through a single UDP port so that peer-to-peer connections can be brokered using the FAF ICE adapter.
The official TA Launcher for [TA Forever (TAF)](https://www.taforever.com/).

## How Total Annihilation work ordinarily
1. Ordinarily, a direcplay application communicates with peers on one tcp port (usually 2300) and one udp port (usually 2350).
   These ports are receive only.
   Therefore request-reply semantics are carried out over two connections with the reply being received on one connection and the reply being send out over another.
   Every directplay tcp request or reply has packaged in its header a reply address and port.

   Game initialisation data (game and player status, unit sync data, chat) is received from peers on the TCP port.
   After launch, in-game data is received from peers on the udp port.
   
   In the ancient directplay dialect of Total Annihilation,
   the application also listens on tcp and udp port 47624 for "enumeration" requests.
   The "enumeration" request serves to discover the existance of a hosted game.
   Also crutially it serves as the primary opportunity for a host to advertise what tcp/udp ports it is listening since that will be embedded in the header of its reply.

## How gpgnet4ta's tunneling works
1. gpgnet4ta masquerades as local instances of TA which are actually proxies for the remote instances.
   By getting the player's TA to connect to these proxies, gpgnet4ta is able to intercept all the directplay/game communications
   and transmit them to peer instances of gpgnet4ta using its own communication mechanisms.
   The peer instances of gpgnet4ta are similarly masquerading as instances of TA to the remote player's instance.
   gpgnet4ta's communication mechanism is to listen on a single UDP port for all traffic destined for the local TA instance.
   It likewise sends data destined for remote TA instances by sending data to a UDP host:port specific to that remote TA instance.
   By using a single UDP port in this way, we're able to leverage the FAF ICE adapter to negotiate a communication pathways between all the peers.
   which itself uses a combination of direct connection (IPv4 or IPv6), UDP hole punching and (if the worst case) proxying through a server.
   In practice the FAF ICE adapter hides all the complexity by just listening on UDP ports on the localhost.
   

1. Main classes involved:
   - gpgnet/GameReceiver:
        Receives data from local instance of TA on behalf of a remote instance of TA.
        One instance running locally per remote TA
   - gpgnet/GameSender:
        Sends data to local instance of TA on behalf of a remote instance of TA.
        One instance running locally per remote TA
   - gpgnet/TafnetNode:
        Communicates game data with peer TafnetNode instances using UDP only.
        One instance running locally for the local TA only listening on one UDP port, communicating with multiple remote TafnetNodes.
   - gpgnet/TafnetGameNode:
        Bridge between GameReceivers/Senders and the TafnetNode.
        One instance running locally for the local TA only.
        Passes data received from GameReceiver to TafnetNode together with corresonding (tafnet)PlayerId and channel (TCP,UDP or ENUM)
        Passes data received from TafnetNode to the right GameSender and channel
        Uses a GameAddressTranslater to forge remote player addresses to match local GameReceivers instead
        Passes packets to a TADemo::TAPacketParser for any other uses - eg GameMonitor2 watches player/game status and victory condition
   - gpgnet/GameAddressTranslater: Fiddles DPlay packets to ensure local TA communicates with GameReceiver instances instead of actual remote TA instances.

1. gpgnet4ta instantiates two classes to intercept all game communications: GameSender and GameReceiver.
   The GameReceiver masquerades as a remote instance of TA by listening on a random tcp port and a random udp port typically (but not necessarily) on interface 127.0.0.1.
   In the case that the game is not expected to be the host, the GameReceiver also takes over tcp port 47624 so as to intercept enumeration requests.
   The GameSender likewise masquerades as a remote instance of TA by sending data to the game on the game's tcp, udp and enumeration ports.
   An instance of a GameSender and GameReceiver is instantiated for every remote instance of TA.
   
1. The TafnetGameNode manages the collection of GameSenders and GameReceivers
   and is aware of the player tafnetIds associated with each pair.
   Data received from game is forwarded to a TafnetNode object along with player tafnetId and receive channel (UDP, TCP or enum).
   TafnetGameNode forges the reply address of data received from TafnetNode.
   It also does a bit more deep inspection of dplay packets in order to forge player addresses contained in various dplay reply messages (SuperEnumReply, CreatePlayerReq, ForwardPlayerReq) to ensure that TA directs all traffic to the correct GameReceiver and to the actual remote game instance.
   
1. The TafnetNode class attaches a one-byte control code to every packet received by a GameReceiver that identifies where it came from: tcp, udp, enumeration port (along with a few other control codes to allow for reliable, ordered communication over unreliable udp).
   It then sends everything to its peer TafnetNode via a single unreliable UDP channel.
   The peer TafnetNode examines the control code and dispatches the data to its own 
   
   
1. We instantiate two classes to communicate with an instance of TA: tafnet::GameSender and tafnet::GameReceiver.
   GameReceiver listens on random 

1. 


## How to run
1. Use [AdoptOpenJDK](https://adoptopenjdk.net/) 14 or Oracle JDK 14 (others might not work)
1. Clone the project with Git
    - using SSH: `git clone git@github.com:ta-forever/downlords-faf-client.git`
    - using HTTPS: `https://github.com/ta-forever/downlords-taf-client.git`
1. Open the project into [IntelliJ IDEA](https://www.jetbrains.com/idea/) Ultimate or Community (free)
1. Make sure you have the IntelliJ IDEA [Lombok plugin](https://plugins.jetbrains.com/idea/plugin/6317-lombok-plugin) installed
1. Make sure you have `Enable annotation processing` enabled in the settings
1. Select `Main` as run configuration next to the hammer button in the top right
1. Compile and start the application by pressing the play button

A video tutorial is available [here](https://www.youtube.com/watch?v=_kJoRehdBcM). Don't forget step 5.
If you want to use the Scene Builder, please import [jfoenix](https://www.youtube.com/watch?v=Di9f_eP_x9I).

### Linux
Learn how to install the client on Linux [here](https://github.com/FAForever/downlords-faf-client/wiki/Install-on-Linux)

(NB: Linux support is mostly likely broken since forking from FAF)

## Open Source licenses 
|                |                               |
|----------------|-------------------------------|
|<img src="https://www.ej-technologies.com/images/product_banners/install4j_large.png" width="128">|Thanks to [ej-technologies](https://www.ej-technologies.com) for their [open source license](https://www.ej-technologies.com/buy/install4j/openSource). We use Install4j to build installers.|
|<img src="https://slack-files2.s3-us-west-2.amazonaws.com/avatars/2017-12-13/286651735269_a5ab3167acef52b0111e_512.png" width="128">| Thanks to [bugsnag](https://www.bugsnag.com) for their [open source license](https://www.bugsnag.com/open-source/). We use bugsnag for our error reporting.|
|<img src="https://faforever.github.io/downlords-faf-client/images/yklogo.png" width="128">| Thanks to [YourKit](https://www.yourkit.com) for their open source license|


## Contribute
Please take a look at (FAF's) [contribution guidelines](https://github.com/FAForever/java-guidelines/wiki/Contribution-Guidelines) before creating a pull request

Have a look at (FAF's) [wiki](https://github.com/FAForever/downlords-faf-client/wiki).


