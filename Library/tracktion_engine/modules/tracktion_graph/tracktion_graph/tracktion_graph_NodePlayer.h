/*
    ,--.                     ,--.     ,--.  ,--.
  ,-'  '-.,--.--.,--,--.,---.|  |,-.,-'  '-.`--' ,---. ,--,--,      Copyright 2018
  '-.  .-'|  .--' ,-.  | .--'|     /'-.  .-',--.| .-. ||      \   Tracktion Software
    |  |  |  |  \ '-'  \ `--.|  \  \  |  |  |  |' '-' '|  ||  |       Corporation
    `---' `--'   `--`--'`---'`--'`--' `---' `--' `---' `--''--'    www.tracktion.com

    Tracktion Engine uses a GPL/commercial licence - see LICENCE.md for details.
*/

#pragma once


namespace tracktion_graph
{

/**
    Simple player for an Node.
    This simply iterate all the nodes attempting to process them in a single thread.
*/
class NodePlayer
{
public:
    /** Creates an empty NodePlayer. */
    NodePlayer() = default;
    
    /** Creates an NodePlayer to process a Node. */
    NodePlayer (std::unique_ptr<Node> nodeToProcess, PlayHeadState* playHeadStateToUse = nullptr)
        : input (std::move (nodeToProcess)), playHeadState (playHeadStateToUse)
    {
    }
    
    /** Returns the current Node. */
    Node* getNode()
    {
        return input.get();
    }

    /** Sets the Node to process. */
    void setNode (std::unique_ptr<Node> newNode)
    {
        setNode (std::move (newNode), sampleRate, blockSize);
    }

    /** Sets the Node to process with a new sample rate and block size. */
    void setNode (std::unique_ptr<Node> newNode, double sampleRateToUse, int blockSizeToUse)
    {
        auto newNodes = prepareToPlay (newNode.get(), input.get(), sampleRateToUse, blockSizeToUse);
        std::unique_ptr<Node> oldNode;
        
        {
            const juce::SpinLock::ScopedLockType sl (inputAndNodesLock);
            oldNode = std::move (input);
            input = std::move (newNode);
            allNodes = std::move (newNodes);
        }
    }

    /** Prepares the current Node to be played. */
    void prepareToPlay (double sampleRateToUse, int blockSizeToUse, Node* oldNode = nullptr)
    {
        allNodes = prepareToPlay (input.get(), oldNode, sampleRateToUse, blockSizeToUse);
    }

    /** Prepares a specific Node to be played and returns all the Nodes. */
    std::vector<Node*> prepareToPlay (Node* node, Node* oldNode, double sampleRateToUse, int blockSizeToUse)
    {
        sampleRate = sampleRateToUse;
        blockSize = blockSizeToUse;
        
        if (playHeadState != nullptr)
            playHeadState->playHead.setScrubbingBlockLength (timeToSample (0.08, sampleRate));
        
        return node_player_utils::prepareToPlay (node, oldNode, sampleRateToUse, blockSizeToUse);
    }

    /** Processes a block of audio and MIDI data.
        Returns the number of times a node was checked but unable to be processed.
    */
    int process (const Node::ProcessContext& pc)
    {
        if (inputAndNodesLock.tryEnter())
        {
            if (! input)
            {
                inputAndNodesLock.exit();
                return 0;
            }
            
            int numMisses = 0;
            
            if (playHeadState != nullptr)
                numMisses = processWithPlayHeadState (*playHeadState, *input, allNodes, pc);
            else
                numMisses = processPostorderedNodes (*input, allNodes, pc);
            
            inputAndNodesLock.exit();
            
            return numMisses;
        }
        
        return 0;
    }
    
    double getSampleRate() const
    {
        return sampleRate;
    }
    
    int processPostorderedNodes (Node& rootNodeToProcess, const std::vector<Node*>& nodes, const Node::ProcessContext& pc)
    {
        return processPostorderedNodesSingleThreaded (rootNodeToProcess, nodes, pc);
    }

protected:
    std::unique_ptr<Node> input;
    PlayHeadState* playHeadState = nullptr;
    
    std::vector<Node*> allNodes;
    double sampleRate = 44100.0;
    int blockSize = 512;
    
    juce::SpinLock inputAndNodesLock;
    
    int processWithPlayHeadState (PlayHeadState& phs, Node& rootNodeToProcess, const std::vector<Node*>& nodes,
                                  const Node::ProcessContext& pc)
    {
        int numMisses = 0;
        
        // Check to see if the timeline needs to be processed in two halves due to looping
        const auto splitTimelineRange = referenceSampleRangeToSplitTimelineRange (phs.playHead, pc.referenceSampleRange);
        
        if (splitTimelineRange.isSplit)
        {
            const auto firstNumSamples = splitTimelineRange.timelineRange1.getLength();
            const auto firstRange = pc.referenceSampleRange.withLength (firstNumSamples);
            
            {
                auto inputAudio = pc.buffers.audio.getStart ((choc::buffer::FrameCount) firstNumSamples);
                auto& inputMidi = pc.buffers.midi;
                
                phs.update (firstRange);
                tracktion_graph::Node::ProcessContext pc1 { firstRange, { inputAudio , inputMidi } };
                numMisses += processPostorderedNodes (rootNodeToProcess, nodes, pc1);
            }
            
            {
                const auto secondNumSamples = splitTimelineRange.timelineRange2.getLength();
                const auto secondRange = juce::Range<int64_t>::withStartAndLength (firstRange.getEnd(), secondNumSamples);
                
                auto inputAudio = pc.buffers.audio.getFrameRange (tracktion_graph::frameRangeWithStartAndLength ((choc::buffer::FrameCount) firstNumSamples, (choc::buffer::FrameCount) secondNumSamples));
                auto& inputMidi = pc.buffers.midi;
                
                //TODO: Use a scratch MidiMessageArray and then merge it back with the offset time
                tracktion_graph::Node::ProcessContext pc2 { secondRange, { inputAudio , inputMidi } };
                phs.update (secondRange);
                numMisses += processPostorderedNodes (rootNodeToProcess, nodes, pc2);
            }
        }
        else
        {
            phs.update (pc.referenceSampleRange);
            numMisses += processPostorderedNodes (rootNodeToProcess, nodes, pc);
        }
        
        return numMisses;
    }

    /** Processes a group of Nodes assuming a postordering VertexOrdering.
        If these conditions are met the Nodes should be processed in a single loop iteration.
    */
    static int processPostorderedNodesSingleThreaded (Node& rootNode, const std::vector<Node*>& allNodes, const Node::ProcessContext& pc)
    {
        for (auto node : allNodes)
            node->prepareForNextBlock (pc.referenceSampleRange);
        
        int numMisses = 0;
        size_t numNodesProcessed = 0;

        for (;;)
        {
            for (auto node : allNodes)
            {
                if (! node->hasProcessed() && node->isReadyToProcess())
                {
                    node->process (pc.referenceSampleRange);
                    ++numNodesProcessed;
                }
                else
                {
                    ++numMisses;
                }
            }

            if (numNodesProcessed == allNodes.size())
            {
                auto output = rootNode.getProcessedOutput();
                auto numAudioChannels = std::min (output.audio.getNumChannels(),
                                                  pc.buffers.audio.getNumChannels());
                
                if (numAudioChannels > 0)
                    add (pc.buffers.audio.getFirstChannels (numAudioChannels),
                         output.audio.getFirstChannels (numAudioChannels));
                
                pc.buffers.midi.mergeFrom (output.midi);

                break;
            }
        }
        
        return numMisses;
    }
};

}
