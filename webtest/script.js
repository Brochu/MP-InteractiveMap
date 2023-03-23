async function init() {
  // Get the canvas element and its WebGL 2 context
  const canvas = document.getElementById('canvas');
  console.log(canvas);
  const context = canvas.getContext('webgpu');
  console.log(context);

  // Create the WebGPU device
  console.log(navigator);
  console.log(navigator.gpu);
  const adapter = await navigator.gpu.requestAdapter();
  const device = await adapter.requestDevice();

  // Set up the render pipeline
  const pipeline = device.createRenderPipeline({
    vertex: {
      module: device.createShaderModule({
        code: `
          [[stage(vertex)]]
          fn main([[location(0)]] position : vec2<f32>) -> [[builtin(position)]] vec4<f32> {
            return vec4<f32>(position, 0.0, 1.0);
          }
        `,
      }),
      entryPoint: 'main',
      buffers: [{
        arrayStride: 2 * 4,
        attributes: [{
          shaderLocation: 0,
          offset: 0,
          format: 'float32x2'
        }]
      }]
    },
    fragment: {
      module: device.createShaderModule({
        code: `
          [[stage(fragment)]]
          fn main() -> [[location(0)]] vec4<f32> {
            return vec4<f32>(1.0, 0.0, 0.0, 1.0);
          }
        `,
      }),
      entryPoint: 'main',
      targets: [{
        format: 'bgra8unorm'
      }]
    },
    primitive: {
      topology: 'triangle-list',
      frontFace: 'ccw',
      cullMode: 'none'
    }
  });

  // Create the command encoder and render pass descriptor
  const commandEncoder = device.createCommandEncoder();
  const renderPassDescriptor = {
    colorAttachments: [{
      view: context.getCurrentTexture().createView(),
      loadValue: [0.0, 0.0, 0.0, 1.0],
      storeOp: 'store'
    }]
  };

  // Encode the commands to draw the triangle
  const passEncoder = commandEncoder.beginRenderPass(renderPassDescriptor);
  passEncoder.setPipeline(pipeline);
  passEncoder.setVertexBuffer(0, vertexBuffer);
  passEncoder.draw(3, 1, 0, 0);
  passEncoder.endPass();

  // Submit the commands to the GPU
  device.queue.submit([commandEncoder.finish()]);
}

init();
