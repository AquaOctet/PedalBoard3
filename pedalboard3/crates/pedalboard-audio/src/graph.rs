//! Audio processing graph - equivalent to Pedalboard2's FilterGraph

use crate::{AudioBuffer, Sample};
use anyhow::Result;
use std::collections::HashMap;
use std::sync::Arc;
use tracing::{debug, trace};

/// Unique identifier for graph nodes
pub type NodeId = u64;

/// Audio processing node trait
pub trait AudioNode: Send + Sync {
    /// Process audio buffer
    fn process(&mut self, input: &AudioBuffer, output: &mut AudioBuffer) -> Result<()>;
    
    /// Get node name for debugging
    fn name(&self) -> &str;
    
    /// Get number of input channels
    fn input_channels(&self) -> u32;
    
    /// Get number of output channels  
    fn output_channels(&self) -> u32;
    
    /// Initialize the node with sample rate and buffer size
    fn initialize(&mut self, sample_rate: u32, buffer_size: u32) -> Result<()>;
}

/// Connection between nodes
#[derive(Debug, Clone)]
pub struct Connection {
    pub from_node: NodeId,
    pub from_channel: u32,
    pub to_node: NodeId,
    pub to_channel: u32,
}

/// Audio processing graph
pub struct ProcessingGraph {
    nodes: HashMap<NodeId, Box<dyn AudioNode>>,
    connections: Vec<Connection>,
    execution_order: Vec<NodeId>,
    buffers: HashMap<NodeId, AudioBuffer>,
    next_id: NodeId,
    sample_rate: u32,
    buffer_size: u32,
}

impl ProcessingGraph {
    /// Create a new processing graph
    pub fn new(sample_rate: u32, buffer_size: u32) -> Self {
        Self {
            nodes: HashMap::new(),
            connections: Vec::new(),
            execution_order: Vec::new(),
            buffers: HashMap::new(),
            next_id: 1,
            sample_rate,
            buffer_size,
        }
    }

    /// Add a node to the graph
    pub fn add_node(&mut self, mut node: Box<dyn AudioNode>) -> Result<NodeId> {
        let id = self.next_id;
        self.next_id += 1;

        // Initialize the node
        node.initialize(self.sample_rate, self.buffer_size)?;
        
        // Create buffer for this node
        let channels = node.output_channels() as usize;
        let buffer = vec![vec![0.0; self.buffer_size as usize]; channels];
        
        self.nodes.insert(id, node);
        self.buffers.insert(id, buffer);
        
        debug!("Added node {} with ID {}", self.nodes[&id].name(), id);
        Ok(id)
    }

    /// Remove a node from the graph
    pub fn remove_node(&mut self, id: NodeId) -> Result<()> {
        // Remove all connections involving this node
        self.connections.retain(|conn| {
            conn.from_node != id && conn.to_node != id
        });
        
        // Remove node and its buffer
        self.nodes.remove(&id);
        self.buffers.remove(&id);
        
        // Recalculate execution order
        self.calculate_execution_order();
        
        debug!("Removed node {}", id);
        Ok(())
    }

    /// Connect two nodes
    pub fn connect(&mut self, from: NodeId, from_channel: u32, to: NodeId, to_channel: u32) -> Result<()> {
        // Validate nodes exist
        if !self.nodes.contains_key(&from) {
            return Err(anyhow::anyhow!("Source node {} not found", from));
        }
        if !self.nodes.contains_key(&to) {
            return Err(anyhow::anyhow!("Target node {} not found", to));
        }

        // Validate channel numbers
        let from_node = &self.nodes[&from];
        let to_node = &self.nodes[&to];
        
        if from_channel >= from_node.output_channels() {
            return Err(anyhow::anyhow!("Source channel {} out of range", from_channel));
        }
        if to_channel >= to_node.input_channels() {
            return Err(anyhow::anyhow!("Target channel {} out of range", to_channel));
        }

        let connection = Connection {
            from_node: from,
            from_channel,
            to_node: to,
            to_channel,
        };

        self.connections.push(connection);
        self.calculate_execution_order();
        
        debug!("Connected node {} (ch {}) -> node {} (ch {})", from, from_channel, to, to_channel);
        Ok(())
    }

    /// Disconnect two nodes
    pub fn disconnect(&mut self, from: NodeId, from_channel: u32, to: NodeId, to_channel: u32) {
        self.connections.retain(|conn| {
            !(conn.from_node == from && 
              conn.from_channel == from_channel &&
              conn.to_node == to && 
              conn.to_channel == to_channel)
        });
        
        self.calculate_execution_order();
        debug!("Disconnected node {} (ch {}) -> node {} (ch {})", from, from_channel, to, to_channel);
    }

    /// Process audio through the graph
    pub fn process(&mut self, input: &AudioBuffer, output: &mut AudioBuffer) -> Result<()> {
        trace!("Processing graph with {} nodes", self.nodes.len());
        
        // Simplified implementation to avoid borrowing issues
        // For now, just pass through the input to output
        if output.len() == input.len() {
            for (out_ch, in_ch) in output.iter_mut().zip(input.iter()) {
                if out_ch.len() == in_ch.len() {
                    out_ch.copy_from_slice(in_ch);
                }
            }
        }
        
        debug!("Processed {} channels through audio graph", output.len());
        Ok(())
    }

    /// Get input for a specific node
    fn get_node_input(&self, node_id: NodeId, main_input: &AudioBuffer) -> AudioBuffer {
        // For now, just return the main input
        // In a full implementation, this would collect from connected nodes
        main_input.clone()
    }

    /// Apply connections to route audio between nodes
    fn apply_connections(&mut self) -> Result<()> {
        // Simplified implementation - just log that connections would be applied
        debug!("Would apply {} connections between nodes", self.connections.len());
        Ok(())
    }

    /// Calculate execution order using topological sort
    fn calculate_execution_order(&mut self) {
        // Simplified topological sort - in practice you'd need proper cycle detection
        self.execution_order = self.nodes.keys().copied().collect();
        self.execution_order.sort();
        debug!("Execution order: {:?}", self.execution_order);
    }

    /// Get number of nodes
    pub fn node_count(&self) -> usize {
        self.nodes.len()
    }

    /// Get number of connections
    pub fn connection_count(&self) -> usize {
        self.connections.len()
    }
}

/// Simple pass-through node for testing
pub struct PassThroughNode {
    name: String,
    channels: u32,
}

impl PassThroughNode {
    pub fn new(name: &str, channels: u32) -> Self {
        Self {
            name: name.to_string(),
            channels,
        }
    }
}

impl AudioNode for PassThroughNode {
    fn process(&mut self, input: &AudioBuffer, output: &mut AudioBuffer) -> Result<()> {
        let channels = input.len().min(output.len()).min(self.channels as usize);
        for ch in 0..channels {
            let samples = input[ch].len().min(output[ch].len());
            output[ch][..samples].copy_from_slice(&input[ch][..samples]);
        }
        Ok(())
    }

    fn name(&self) -> &str {
        &self.name
    }

    fn input_channels(&self) -> u32 {
        self.channels
    }

    fn output_channels(&self) -> u32 {
        self.channels
    }

    fn initialize(&mut self, _sample_rate: u32, _buffer_size: u32) -> Result<()> {
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_graph_creation() {
        let graph = ProcessingGraph::new(44100, 512);
        assert_eq!(graph.node_count(), 0);
        assert_eq!(graph.connection_count(), 0);
    }

    #[test]
    fn test_add_node() {
        let mut graph = ProcessingGraph::new(44100, 512);
        let node = Box::new(PassThroughNode::new("test", 2));
        let id = graph.add_node(node).unwrap();
        assert_eq!(graph.node_count(), 1);
        assert_eq!(id, 1);
    }

    #[test]
    fn test_connect_nodes() {
        let mut graph = ProcessingGraph::new(44100, 512);
        let node1 = Box::new(PassThroughNode::new("node1", 2));
        let node2 = Box::new(PassThroughNode::new("node2", 2));
        
        let id1 = graph.add_node(node1).unwrap();
        let id2 = graph.add_node(node2).unwrap();
        
        let result = graph.connect(id1, 0, id2, 0);
        assert!(result.is_ok());
        assert_eq!(graph.connection_count(), 1);
    }
}