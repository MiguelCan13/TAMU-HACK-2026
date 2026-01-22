### teehee, really useful:
### https://docs.ultralytics.com/modes/track/
###
from ultralytics import YOLO


# test = model.track(
#     source = 0,  #use personal webcam
#     conf = .25,  #confidence threshold
#     show = True  #display results
# )

###CHANGE MODEL IF YOU ALREADY HAVE A TRAINED ONE
model = YOLO("yolo11n-cls.pt") #model for classification


# Train the model
results = model.train(
    data="ultralytics/car orientation data",
    epochs=1,               # Number of training epochs (cycles)
    imgsz=224,               
    name="car_orientation",   # Name of the training run
    save=True,               # Save checkpoints
    plots=True,              # Generate training plots
    device="cpu",                # Use GPU 0 (change to 'cpu' if no GPU)
)

# Save the trained model
model.save("car_orientation_model.pt")

print("\n" + "="*50)
print("Training Complete!")
print(f"Best model saved at: runs/classify/car_orientation/weights/best.pt")
print("="*50)