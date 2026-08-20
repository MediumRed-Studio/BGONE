[ComponentEditorProps(category: "GameScripted/Vehicles/BGONE", description: "Moves vehicle forward and backward for missile guidance target testing")]
class BGONE_VehicleAutoMoveComponentClass : ScriptGameComponentClass
{
}

class BGONE_VehicleAutoMoveComponent : ScriptGameComponent
{
	[Attribute("10", UIWidgets.Slider, "Speed in m/s", "0 100 1", category: "BGONE")]
	protected float m_fSpeed;
	
	[Attribute("100", UIWidgets.Slider, "Travel distance before reversing in meters", "10 5000 10", category: "BGONE")]
	protected float m_fTravelDistance;
	
	protected IEntity m_eOwner;
	protected vector m_vInitialDir;
	protected bool m_bMovingForward = true;
	protected float m_fDistanceTraveled = 0;
	protected RplComponent m_RplComponent;
	protected Physics m_Physics;
	
	override void OnPostInit(IEntity owner)
	{
		SetEventMask(owner, EntityEvent.INIT | EntityEvent.FIXEDFRAME);
	}
	
	override void EOnInit(IEntity owner)
	{
		m_eOwner = owner;
		if (m_eOwner)
		{
			m_RplComponent = RplComponent.Cast(m_eOwner.FindComponent(RplComponent));
			m_Physics = m_eOwner.GetPhysics();
			
			vector mat[4];
			m_eOwner.GetWorldTransform(mat);
			m_vInitialDir = mat[2];
		}
	}
	
	override void EOnFixedFrame(IEntity owner, float timeSlice)
	{
		if (!m_eOwner || m_fSpeed <= 0)
			return;
			
		if (m_RplComponent && !m_RplComponent.IsMaster())
			return;
			
		if (!m_Physics)
			m_Physics = m_eOwner.GetPhysics();
			
		if (!m_Physics)
			return;
			
		float step = m_fSpeed * timeSlice;
		m_fDistanceTraveled += step;
		
		if (m_fTravelDistance > 0 && m_fDistanceTraveled >= m_fTravelDistance)
		{
			m_fDistanceTraveled = 0;
			m_bMovingForward = !m_bMovingForward;
		}
		
		vector moveDir = m_vInitialDir;
		if (!m_bMovingForward)
			moveDir = -m_vInitialDir;
			
		m_Physics.SetVelocity(moveDir * m_fSpeed);
	}
}
