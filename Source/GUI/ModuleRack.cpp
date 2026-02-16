#include "ModuleRack.h"
#include "../PluginProcessor.h"
#include "../MasteringModule.h"
#include "ModuleWrapper.h"

using namespace oxygen;

ModuleRack::ModuleRack(OxygenAudioProcessor& p) : audioProcessor(p)
{
    contentComponent = std::make_unique<juce::Component>();
    setViewedComponent(contentComponent.get(), false);
    
    setScrollBarsShown(true, false);
}

ModuleRack::~ModuleRack()
{
}

void ModuleRack::updateModuleList()
{
    moduleWrappers.clear();
    contentComponent->removeAllChildren();
    
    const auto& nodes = audioProcessor.getModuleNodes();
    
    for (int i = 0; i < (int)nodes.size(); ++i)
    {
        auto* node = nodes[i].get();
        if (auto* processor = node->getProcessor())
        {
            auto name = processor->getName();
            juce::RangedAudioParameter* bypassParam = nullptr;
            
            if (auto* mm = dynamic_cast<oxygen::MasteringModule*>(processor))
                bypassParam = mm->getBypassParameter();
            
            if (processor->hasEditor())
            {
                auto* wrapper = new oxygen::ModuleWrapper(name, std::unique_ptr<juce::AudioProcessorEditor>(processor->createEditor()), bypassParam);
                moduleWrappers.add(wrapper);
                contentComponent->addAndMakeVisible(wrapper);
                
                wrapper->onHeightChanged = [this] { resized(); };
                
                // Use a copy of i to avoid scope issues in lambda
                wrapper->onMoveUp   = [this, i] { audioProcessor.moveModuleUp(i);   updateModuleList(); };
                wrapper->onMoveDown = [this, i] { audioProcessor.moveModuleDown(i); updateModuleList(); };
            }
        }
    }
    
    resized();
}

void ModuleRack::resized()
{
    juce::Viewport::resized();
    
    int y = 0;
    int width = getWidth() - (getVerticalScrollBar().isVisible() ? getVerticalScrollBar().getWidth() : 0);
    
    for (auto* child : contentComponent->getChildren())
    {
        child->setBounds(0, y, width, child->getHeight());
        y += child->getHeight() + 10;
    }
    
    contentComponent->setSize(width, y);
}
